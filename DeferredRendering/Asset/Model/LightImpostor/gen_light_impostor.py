import os
import json

SCENE_FILE = "../../Scene/main.json"
MODEL_FILE = "./Model"
MATERIAL_FILE = "./Material"

IMPOSTOR_NAME = "./impostor_list.json"
IMPOSTOR_SPHERE_SIZE = 0.1


def gen_impostor():
    with open(SCENE_FILE) as f:
        scene_file = json.load(f)
        light_objs = []

        for index, light in enumerate(scene_file["mSceneLight"]):
            color = light["mColor"]
            intensity = light["mIntensity"]

            with open("{}/{}".format(MATERIAL_FILE, "light_impostor_mat_{}.json".format(index)), "w") as f:
                json.dump({
                        "mParameterTable": 
                        {
                            "Roughness": 0.0,
                            "Metallic": 0.0,
                            "Albedo": [color["x"], color["y"], color["z"]],
                            "Emission": intensity
                        },
                        "mShaderPath": "gbuffer.hlsl",
                        "@IResource": {},
                        "mTexturePath": {}
                    }, f, indent=4
                )

            with open("{}/{}".format(MODEL_FILE, "light_impostor_model_{}.json".format(index)), "w") as f:
                json.dump({
                        "mMaterialPath": [
                            "Asset/Model/LightImpostor/Material/light_impostor_mat_{}".format(index)
                        ],
                        "@IResource": {},
                        "mMeshPath": "Asset/Model/Sphere/sphere_Mesh"
                    }, f, indent=4
                )

            light_objs.append({
                "@SceneObject": {
                    "mName": "light_impostor_{}".format(index),
                    "mTranslation": light["@SceneObject"]["mTranslation"],
                    "mRotation":light["@SceneObject"]["mRotation"],
                    "mScale": {"x": 0.1, "y": 0.1, "z": 0.1}
                },
                "mModelFilePath": "Asset/Model/LightImpostor/Model/light_impostor_model_{}.json".format(index)
            })


        with open(IMPOSTOR_NAME, "w") as f:
            json.dump(light_objs, f, indent=4)


if __name__ == "__main__":
    gen_impostor()









