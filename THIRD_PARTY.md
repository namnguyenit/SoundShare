**Pinned third-party dependencies**

| Component | Pin | License | Use |
| --- | --- | --- | --- |
| libsamplerate | 0.2.2, commit b9c20b93660c3683fda12e3c2a01f0021bf96c56 | BSD 2-Clause | Streaming sample-rate conversion |
| nlohmann/json | 3.12.0, commit 65ee68451d8eb2b5f3a30b410476ab83deb3289b | MIT | Control/config JSON, outside realtime callbacks |
| Microsoft.Windows.CppWinRT | 2.0.240405.15 | MIT | Generated native Windows Runtime projections |

Archives are fetched by CMake from upstream GitHub/NuGet with SHA-256 verification. C++/WinRT generation runs locally against the installed Windows SDK metadata. The old MSVC 19.29 compiler uses the supported WINRT_NO_SOURCE_LOCATION switch to avoid a consteval compiler ICE; this disables extra exception metadata, not audio functionality or C++20.

License texts are retained under `third_party/licenses`. Windows SDK/MSVC and their redistributables have their own distribution terms. No VB-CABLE binary, Bluetooth codec implementation, driver installer or third-party audio application is bundled.
