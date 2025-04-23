#DirectXTex build options
Set(BUILD_TOOLS OFF)
Set(BUILD_SAMPLE OFF)
Set(BUILD_TESTING OFF)

add_subdirectory(DirectXTex)
add_subdirectory(DirectXShaderCompiler)

# set visual studio folder
set_target_properties(DirectXTex PROPERTIES FOLDER "Vendor/DirectXTex")