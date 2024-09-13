if Project "harmony" then
    Compile "src/**"
    Include "src"
    Import "simdjson"
    Artifact { "out/harmony", type = "Console" }
end
