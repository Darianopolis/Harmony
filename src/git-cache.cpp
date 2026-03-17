#include "git-cache.hpp"
#include "harmony.hpp"
#include "sha256.hpp"

#include <git2.h>

#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#define GIT_CACHE_VERBOSE 1

#if GIT_CACHE_VERBOSE
#define VERBOSE_LOG(fmt, ...) log("[VERBOSE] " fmt __VA_OPT__(,) __VA_ARGS__)
#else
#define VERBOSE_LOG(...)
#endif

namespace fs = std::filesystem;

template<typename Fn>
struct DeferGuard
{
    Fn fn;

    DeferGuard(Fn&& fn): fn(std::move(fn)) {}
    ~DeferGuard() { fn(); };
};

#define defer DeferGuard _ = [&]

auto ptr_to(auto&& v) { return &v; }

namespace
{

// ---------------------------------------------------------------------------

template <auto Fn>
struct GitDeleter
{
    template <typename T>
    auto operator()(T* p) const -> void { Fn(p); }
};

using Repository = std::unique_ptr<git_repository, GitDeleter<git_repository_free>>;
using Object     = std::unique_ptr<git_object,     GitDeleter<git_object_free>>;

// ---------------------------------------------------------------------------

struct Directories
{
    fs::path metadata;
    fs::path checkout;
};

// ---------------------------------------------------------------------------

auto check_git(int rc, std::string_view context) -> void
{
    if (rc < 0) {
        const git_error* err = git_error_last();
        std::string msg = err ? err->message : "unknown error";
        throw std::runtime_error(std::format("{}: {}", context, msg));
    }
}

auto oid_to_string(const git_oid* oid) -> std::string
{
    char buf[GIT_OID_MAX_HEXSIZE + 1];
    return git_oid_tostr(buf, sizeof(buf), oid);
}

// ---------------------------------------------------------------------------

auto get_metadata_repo(const Directories& dirs, const std::string& url) -> Repository
{
    fs::path repo_path = dirs.metadata / harmony::sha256_hex(url);

    VERBOSE_LOG("get_metadata_repo(url = \"{}\")", url);
    VERBOSE_LOG("  repo_path: \"{}\"", repo_path.c_str());

    if (fs::exists(repo_path)) {
        VERBOSE_LOG("  repo exists, opening");
        git_repository* repo;
        check_git(
            git_repository_open(&repo, repo_path.c_str()),
            "open metadata repo");
        VERBOSE_LOG("  opened: {}", (void*)repo);
        return Repository(repo);
    }

    log("[metadata] Cloning metadata repo for {}", url);

    git_clone_options opts = GIT_CLONE_OPTIONS_INIT;
    opts.checkout_opts.checkout_strategy = GIT_CHECKOUT_NONE;

    git_repository* raw = nullptr;
    check_git(
        git_clone(&raw, url.c_str(), repo_path.c_str(), &opts),
        std::format("clone metadata repo for {}", url));
    return Repository(raw);
}

// ---------------------------------------------------------------------------

auto is_remote_branch(git_repository* repo, std::string_view maybe_branch) -> bool
{
    VERBOSE_LOG("is_remote_branch(repo = {}, maybe_branch = \"{}\")", (void*)repo, maybe_branch);

    git_branch_iterator* iter = nullptr;
    if (git_branch_iterator_new(&iter, repo, GIT_BRANCH_REMOTE) < 0) { return false; }
    defer { git_branch_iterator_free(iter); };

    auto target = std::format("origin/{}", maybe_branch);

    VERBOSE_LOG("  target = \"{}\"", target);

    git_reference* branch = nullptr;
    while (git_branch_next(&branch, ptr_to<git_branch_t>({}), iter) == 0) {
        defer { git_reference_free(branch); };
        const char* name = nullptr;
        if (git_branch_name(&name, branch) == 0) {
            VERBOSE_LOG("  branch = \"{}\"", name);
            if (name == target) { return true; }
        }
    }
    return false;
}

auto fetch_remote(git_repository* repo, const char* ref) -> void
{
    VERBOSE_LOG("fetch_remote(repo = {}, ref = {})", (void*)repo, ref);

    git_remote* remote = nullptr;
    check_git(git_remote_lookup(&remote, repo, "origin"), "lookup remote");
    defer { git_remote_free(remote); };
    VERBOSE_LOG("  remote = {}", (void*)remote);

    // Use a full mapping refspec so the result lands in refs/remotes/origin/<branch>
    // and is discoverable via revparse("origin/<branch>").
    auto refspec = std::format("+refs/heads/{0}:refs/remotes/origin/{0}", ref);

    git_fetch_options opts = GIT_FETCH_OPTIONS_INIT;
    check_git(
        git_remote_fetch(remote,
            ptr_to<git_strarray>({const_cast<char**>(ptr_to(refspec.c_str())), 1}),
            &opts,
            nullptr),
        std::format("fetch ref {}", ref));
}

auto object_lookup(git_repository* repo, const git_oid* oid)
{
    git_object* obj = nullptr;
    git_object_lookup(&obj, repo, oid, GIT_OBJECT_ANY);
    return Object(obj);
}

auto revparse_single(git_repository* repo, const char* ref) -> Object
{
    git_object* obj = nullptr;
    if (git_revparse_single(&obj, repo, ref) != 0) {
        VERBOSE_LOG("revparse_single({}) FAILED: {}", ref, git_error_last()->message);
    }
    return Object(obj);
}

auto resolve_ref(git_repository* repo, const char* ref, bool fetch) -> Object
{
    VERBOSE_LOG("resolve_ref(repo = {}, ref = \"{}\", fetch = {})", (void*)repo, ref, fetch);

    auto refspec_len = strlen(ref);
    if (refspec_len == 40 || refspec_len == 64) {
        VERBOSE_LOG("  ref is {} characters, probably an OID", refspec_len);

        char maybe_hex_oid[64] = {};
        std::memcpy(maybe_hex_oid, ref, refspec_len);

        // ref is probably an OID
        git_oid oid = {};
        if (git_oid_fromstr(&oid, maybe_hex_oid) == 0) {
            if (auto obj = object_lookup(repo, &oid)) {
                VERBOSE_LOG("  found OID directly");
                return obj;
            }
        }

        VERBOSE_LOG("  failed to find OID directly");
    } else {
        VERBOSE_LOG("  ref is not 40 characters, possibly branch, tag or OID fragment");
    }

    bool fetched = false;
    if (fetch && is_remote_branch(repo, ref)) {
        log("[checkout] Fetching updated branch content for \"{}\"", ref);
        fetch_remote(repo, ref);
        fetched = true;
    }

    auto try_revparse = [&](const char* r) -> Object {
        if (auto obj = revparse_single(repo, r)) {
            VERBOSE_LOG("  found OID via \"{}\": {}", r, oid_to_string(git_object_id(obj.get())));
            return obj;
        }

        // Also try as a remote-tracking branch (refs/remotes/origin/<ref>)
        auto remote_ref = std::format("origin/{}", r);
        if (auto obj = revparse_single(repo, remote_ref.c_str())) {
            VERBOSE_LOG("  found OID via \"{}\": {}", remote_ref, oid_to_string(git_object_id(obj.get())));
            return obj;
        }
        return {};
    };

    VERBOSE_LOG("  first revparse attempt");
    if (auto obj = try_revparse(ref)) { return obj; }

    if (!fetched) {
        log("[metadata] ref \"{}\" not found locally, attempting direct fetch", ref);
        fetch_remote(repo, ref);

        VERBOSE_LOG("  second revparse attempt");
        if (auto obj = try_revparse(ref)) { return obj; }
    }

    VERBOSE_LOG("  all revparse attempts failed");

    throw std::runtime_error(std::format("[metadata] ref \"{}\" could not be resolved or fetched.", ref));
}

// ---------------------------------------------------------------------------

auto make_checkout_path(const Directories& dirs, std::string_view url, std::string_view oid_str)
{
    return dirs.checkout / harmony::sha256_hex(std::format("{}@{}", url, oid_str));
}

// ---------------------------------------------------------------------------

struct SubmodulePayload {
    const Directories& dirs;
    git_repository* repo;
    const std::string& url;
};

auto process_submodules(
    const Directories& dirs,
    git_repository* repo,
    const std::string& url
) -> void
{
    auto callback = [](git_submodule* sm, const char* /*name*/, void* raw) -> int {
        auto& p = *static_cast<SubmodulePayload*>(raw);

        const git_oid* pinned_oid = git_submodule_head_id(sm);
        if (!pinned_oid) { return 0; }

        git_buf resolved_url = GIT_BUF_INIT;
        defer { git_buf_dispose(&resolved_url); };
        check_git(
            git_submodule_resolve_url(&resolved_url, p.repo, git_submodule_url(sm)),
            "resolve submodule URL");

        auto submodule_ref = oid_to_string(pinned_oid);
        auto[submodule_path, _] = harmony::git_cache::checkout(
            p.dirs.checkout.parent_path(),
            resolved_url.ptr,
            submodule_ref);

        make_symlink(
            fs::path(git_repository_workdir(p.repo)) / git_submodule_path(sm),
            submodule_path,
            /*force=*/true);

        return 0;
    };

    SubmodulePayload payload{dirs, repo, url};
    check_git(git_submodule_foreach(repo, callback, &payload), "iterate submodules");
}

// ---------------------------------------------------------------------------

auto do_checkout(
    const Directories& dirs,
    git_repository* metadata_repo,
    const std::string& url,
    git_object* metadata_object
) -> harmony::git_cache::CheckoutResult
{
    auto oid = git_object_id(metadata_object);
    auto object_oid_str = oid_to_string(oid);

    VERBOSE_LOG("do_checkout({}, {})", url, object_oid_str);

    fs::path checkout_dir = make_checkout_path(dirs, url, object_oid_str);

    if (fs::exists(checkout_dir)) {
        return {checkout_dir, object_oid_str};
    }

    log("[checkout] Checking out {}", object_oid_str);

    // Clone from local metadata repo with no working tree yet
    git_clone_options clone_opts = GIT_CLONE_OPTIONS_INIT;
    clone_opts.checkout_opts.checkout_strategy = GIT_CHECKOUT_NONE;

    auto metadata_path = git_repository_path(metadata_repo);

    git_repository* raw_repo = nullptr;
    check_git(
        git_clone(&raw_repo, metadata_path, checkout_dir.c_str(), &clone_opts),
        "clone checkout repo");
    Repository repo(raw_repo);

    // Get checked out repo object
    auto object = object_lookup(raw_repo, oid);
    VERBOSE_LOG("  object = {}", (void*)object.get());

    // Fix origin URL so git_submodule_resolve_url resolves relative URLs correctly
    check_git(
        git_remote_set_url(repo.get(), "origin", url.c_str()),
        "set origin URL");

    // Populate the working tree
    git_checkout_options co_opts = GIT_CHECKOUT_OPTIONS_INIT;
    co_opts.checkout_strategy = GIT_CHECKOUT_FORCE;
    check_git(
        git_checkout_tree(repo.get(), object.get(), &co_opts),
        "checkout tree");

    // Point HEAD at the commit (detached)
    check_git(
        git_repository_set_head_detached(repo.get(), oid),
        "set HEAD detached");

    process_submodules(dirs, repo.get(), url);

    return {checkout_dir, object_oid_str};
}

} // anonymous namespace

namespace harmony::git_cache
{

auto checkout(
    const fs::path& cache_dir,
    const std::string& url,
    const std::string& ref,
    bool fetch
) -> CheckoutResult
{
    // Init libgit2
    git_libgit2_init();

    // Init cache directories
    Directories dirs {
        .metadata = cache_dir / "metadata",
        .checkout = cache_dir / "checkout",
    };
    fs::create_directories(dirs.metadata);
    fs::create_directories(dirs.checkout);

    // Check if ref is already a full commit hash and checkout exists
    auto checkout = make_checkout_path(dirs, url, ref);
    if (fs::exists(checkout)) { return {checkout, ref}; }

    // Else lookup refspec in metadata repo
    auto metadata = get_metadata_repo(dirs, url);
    auto commit   = resolve_ref(metadata.get(), ref.c_str(), fetch);
    return do_checkout(dirs, metadata.get(), url, commit.get());
}

} // namespace harmony::git_cache
