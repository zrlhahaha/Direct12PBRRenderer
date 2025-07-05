import json
from sphere_def import *

def __main__():
    OUTPUT_FILE = "sphere_scene_section.json"
    PIVOT = (0, 2, 5) # position of the bottom left corner of the sphere grid
    SPACING = 2.0
    SCALE = 0.5

    with open(OUTPUT_FILE, "w") as f:
        sections = []

        for roughness in range(NUM_ROUGHNESS):
            for metallic in range(NUM_METALLIC):
                pos = (
                    PIVOT[0] + roughness * SPACING,
                    PIVOT[1] + metallic  * SPACING,
                    PIVOT[2],
                )

                sections.append(
                    {
                        "@SceneObject": {
                            "mName": "sphere_R{}_M{}".format(roughness, metallic),
                            "mRotation": {
                                "x": 0.0,
                                "y": 0.0,
                                "z": 0.0
                            },
                            "mScale": {
                                "x": SCALE,
                                "y": SCALE,
                                "z": SCALE
                            },
                            "mTranslation": {
                                "x": pos[0],
                                "y": pos[1],
                                "z": pos[2]
                            }
                        },
                        "mModelFilePath": "{}/{}/{}".format(ROOT_FOLDER, MODEL_FOLDER, "sphere_R{}_M{}".format(roughness, metallic)),
                    }
                )

        json.dump(
            sections,
            f,
            indent=4
        )

if __name__ == "__main__":
    __main__()