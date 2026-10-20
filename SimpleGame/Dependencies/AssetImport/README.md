# Asset import dependencies

Vendored, unmodified single-file releases. No package installation or runtime download is needed.

| File | Version | Source | SHA-256 |
| --- | --- | --- | --- |
| cgltf.h | 1.15 | https://github.com/jkuhlmann/cgltf/tree/v1.15 | E378A21C084BF1F288BB799DE827BB26906EFB024255F1ECF1705EA13F11C6EC |
| json.hpp | 3.12.0 | https://github.com/nlohmann/json/tree/v3.12.0 | AAF127C04CB31C406E5B04A63F1AE89369FCCDE6D8FA7CDDA1ED4F32DFC5DE63 |

Both libraries use the MIT license. Original copyright and license notices remain in each header. cgltf implementation is compiled exactly once in AssetModel.cpp; JSON is used only for metadata/catalog parsing in AssetLibrary.cpp. Application-side checks narrow supported glTF features to the authored LowPolyKit profile.
