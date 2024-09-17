if Project "harmony" then
    Compile "src/**"
    Include "src"
    Import {
        "yyjson",
        "curl",
    }
    Artifact { "out/harmony", type = "Console" }
end
