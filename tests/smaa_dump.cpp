// Writes the SMAA shader texts of src/ui/smaa_shaders.h as files:
//   smaa_*_body.inc   the bodies, which tests/test_smaa.cpp includes and runs as C++ (the very text the DLL hands to
//                     the HLSL compiler);
//   smaa_*.hlsl       the whole shaders as the DLL builds them (the weight shader with the longest walk), for
//                     "make hlslcheck" or for trying them in any HLSL compiler by hand.
#include <stdio.h>
#include <string>
#include "ui/smaa_shaders.h"

static bool Put(const std::string& dir, const char* name, const char* text) {
    const std::string path = dir + "/" + name;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        printf("cannot write %s\n", path.c_str());
        return false;
    }
    fputs(text, f);
    fclose(f);
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: smaa_dump <output folder>\n");
        return 2;
    }
    const std::string dir = argv[1];
    const bool ok = Put(dir, "smaa_edge_body.inc", UFX_SMAA_EDGE_BODY) && Put(dir, "smaa_weight_body.inc", UFX_SMAA_WEIGHT_BODY) &&
                    Put(dir, "smaa_blend_body.inc", UFX_SMAA_BLEND_BODY) &&
                    Put(dir, "smaa_edge.hlsl", UFX_SMAA_EDGE_DECL UFX_SMAA_EDGE_BODY UFX_SMAA_EDGE_MAIN) &&
                    Put(dir, "smaa_weight.hlsl", "#define SMAA_STEPS 16\n" UFX_SMAA_WEIGHT_DECL UFX_SMAA_WEIGHT_BODY UFX_SMAA_WEIGHT_MAIN) &&
                    Put(dir, "smaa_blend.hlsl", UFX_SMAA_BLEND_DECL UFX_SMAA_BLEND_BODY UFX_SMAA_BLEND_MAIN);
    return ok ? 0 : 1;
}
