if Project "harmony" then
    Compile "src/**"
    Include "src"
    Import "yyjson"
    Artifact { "out/harmony", type = "Console" }
end
