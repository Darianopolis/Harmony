#include "git-cache.hpp"
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

#define GIT_CACHE_VERBOSE 1

#if GIT_CACHE_VERBOSE
#define VERBOSE_LOG(fmt, ...) std::println("[VERBOSE] " fmt __VA_OPT__(,) __VA_ARGS__)
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

    std::println("[metadata] Cloning metadata repo for {}", url);

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

auto fetch_remote(git_repository* repo, const char* refspec) -> void
{
    VERBOSE_LOG("fetch_remote(repo = {}, refspec = {})", (void*)repo, refspec);

    git_remote* remote = nullptr;
    check_git(git_remote_lookup(&remote, repo, "origin"), "lookup remote");
    defer { git_remote_free(remote); };
    VERBOSE_LOG("  remote = {}", (void*)remote);

    check_git(
        git_remote_fetch(remote,
            ptr_to<git_strarray>({const_cast<char**>(&refspec), 1}),
            ptr_to<git_fetch_options>(GIT_FETCH_OPTIONS_INIT),
            nullptr),
        std::format("fetch ref {}", refspec));
}

auto revparse_single(git_repository* repo, const char* ref) -> Object
{
    git_object* obj = nullptr;
    git_revparse_single(&obj, repo, ref);
    return Object(obj);
}

auto resolve_ref(git_repository* repo, const char* refspec, bool fetch) -> Object
{
    VERBOSE_LOG("resolve_ref(repo = {}, refspec = \"{}\", fetch = {})", (void*)repo, refspec, fetch);

    auto refspec_len = strlen(refspec);
    if (refspec_len == 40 || refspec_len == 64) {
        VERBOSE_LOG("  refspec is {} characters, probably an OID", refspec_len);

        char maybe_hex_oid[64] = {};
        std::memcpy(maybe_hex_oid, refspec, refspec_len);

        // ref is probably an OID
        git_oid oid = {};
        if (git_oid_fromstr(&oid, maybe_hex_oid) == 0) {
            if (auto obj = revparse_single(repo, refspec)) {
                VERBOSE_LOG("  found OID directly");
                return obj;
            }
        }

        VERBOSE_LOG("  failed to find OID directly");
    } else {
        VERBOSE_LOG("  refspec is not 40 characters, possibly branch, tag or OID fragment");
    }

    bool fetched = false;
    if (fetch && is_remote_branch(repo, refspec)) {
        std::println("[checkout] Fetching updated branch content for \"{}\"", refspec);
        fetch_remote(repo, refspec);
        fetched = true;
    }

    VERBOSE_LOG("  first revparse attempt");
    if (auto obj = revparse_single(repo, refspec)) {
        VERBOSE_LOG("  found OID : {}", oid_to_string(git_object_id(obj.get())));
        return obj;
    }

    if (!fetched) {
        std::println("[metadata] refspec \"{}\" not found locally, attempting direct fetch", refspec);
        fetch_remote(repo, refspec);

        VERBOSE_LOG("  second revparse attempt");
        if (auto obj = revparse_single(repo, refspec)) {
            VERBOSE_LOG("  found OID : {}", oid_to_string(git_object_id(obj.get())));
            return obj;
        }
    }

    VERBOSE_LOG("  all revparse attempts failed");

    throw std::runtime_error(std::format("[metadata] refspec \"{}\" could not be resolved or fetched.", refspec));
}

// ---------------------------------------------------------------------------

auto make_checkout_path(const Directories& dirs, std::string_view url, std::string_view oid_str)
{
    return dirs.checkout / harmony::sha256_hex(std::format("{}@{}", url, oid_str));
}

auto do_checkout(
    const Directories& dirs,
    git_repository* metadata_repo,
    const std::string& url,
    git_object* object
) -> fs::path
{
    auto oid = git_object_id(object);
    auto object_oid_str = oid_to_string(oid);

    fs::path checkout_dir = make_checkout_path(dirs, url, object_oid_str);

    if (fs::exists(checkout_dir)) { return checkout_dir; }

    std::println("[checkout] Checking out {}", object_oid_str);

    // Clone from local metadata repo with no working tree yet
    git_clone_options clone_opts = GIT_CLONE_OPTIONS_INIT;
    clone_opts.checkout_opts.checkout_strategy = GIT_CHECKOUT_NONE;

    auto metadata_path = git_repository_path(metadata_repo);

    git_repository* raw_repo = nullptr;
    check_git(
        git_clone(&raw_repo, metadata_path, checkout_dir.c_str(), &clone_opts),
        "clone checkout repo");
    Repository repo(raw_repo);

    // Populate the working tree
    git_checkout_options co_opts = GIT_CHECKOUT_OPTIONS_INIT;
    co_opts.checkout_strategy = GIT_CHECKOUT_FORCE;
    check_git(
        git_checkout_tree(repo.get(), object, &co_opts),
        "checkout tree");

    // Point HEAD at the commit (detached)
    check_git(
        git_repository_set_head_detached(repo.get(), oid),
        "set HEAD detached");

    return checkout_dir;
}

} // anonymous namespace

namespace harmony::git_cache
{

auto checkout(
    const fs::path& cache_dir,
    const std::string& url,
    const std::string& ref,
    bool fetch
) -> fs::path
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
    if (fs::exists(checkout)) { return checkout; }

    // Else lookup refspec in metadata repo
    auto metadata = get_metadata_repo(dirs, url);
    auto commit   = resolve_ref(metadata.get(), ref.c_str(), fetch);
    return do_checkout(dirs, metadata.get(), url, commit.get());
}

} // namespace harmony::git_cache
