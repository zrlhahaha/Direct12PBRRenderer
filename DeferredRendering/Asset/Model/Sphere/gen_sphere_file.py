import json
import os
from sphere_def import *


def main():
    os.makedirs(MATERIAL_FOLDER, exist_ok=True)
    os.makedirs(MODEL_FOLDER, exist_ok=True)
    assert(os.path.exists(MESH_FILE + ".bin"))

    # generate sphere materials with different roughness and metallic values
    for roughness in range(NUM_ROUGHNESS):
        for metallic in range(NUM_METALLIC):
            with open("{}/sphere_R{}_M{}.json".format(MATERIAL_FOLDER, roughness, metallic), "w") as f:
                json.dump(
                    {
                        "@IResource": {},
                        "mParameterTable": 
                        {
                            "Albedo" : [1.0, 1.0, 1.0],
                            "Roughness": roughness / float(NUM_ROUGHNESS - 1),
                            "Metallic": metallic / float(NUM_METALLIC - 1),
                        },
                        "mShaderPath": "gbuffer.hlsl",
                        "mTexturePath": {}
                    }
                , f, indent=4)

    # generate sphere models
    for roughness in range(NUM_ROUGHNESS):
        for metallic in range(NUM_METALLIC):
            with open("{}/sphere_R{}_M{}.json".format(MODEL_FOLDER, roughness, metallic), "w") as f:
                json.dump(
                    {
                        "@IResource": {},
                        "mMaterialPath": [
                            "{}/{}/sphere_R{}_M{}.json".format(ROOT_FOLDER, MATERIAL_FOLDER, roughness, metallic)
                        ],
                        "mMeshPath": "{}/{}".format(ROOT_FOLDER, MESH_FILE),
                    }
                , f, indent=4)

if __name__ == "__main__":
    main()