# SoundShare

Ứng dụng âm thanh Windows viết bằng **C++20**, gồm giao diện Win32 native và engine WASAPI chạy trong tiến trình riêng.

- Chia sẻ âm thanh tới nhiều đầu ra Windows được chọn.
- Âm lượng/mute/delay từng nhánh, bộ đệm độc lập và bù lệch clock.
- Quản lý nhận Bluetooth từ điện thoại/TV box qua API Windows.
- Lưu cấu hình, chẩn đoán JSON và khay hệ thống.

**Trạng thái: bản kỹ thuật 0.2.1.** Đã kiểm thử core, model, giao thức, vòng đời receiver mô phỏng và stream WASAPI thật ở âm lượng 0. Chưa xác nhận hai nguồn Bluetooth cùng truyền ra hai tai nghe Bluetooth; chưa có driver âm thanh ảo riêng. Xem [kết quả kiểm thử trước khi đưa lên GitHub](docs/PREPUBLISH_TESTS.vi.md).

## Chạy và build

Giải nén gói Windows x64 và mở `bin/SoundShare.Desktop.exe`. Giữ worker cạnh giao diện. Cần Windows x64 và Visual C++ Runtime x64 tương thích VS 2019 trở lên.

Trên máy phát triển có MSVC C++20, Windows SDK và CMake:

```powershell
.\tools\build.ps1
.\tools\test.ps1
```

Preset dùng VS 2019. Với VS 2022:

```powershell
cmake -S . -B out/build/vs2022 -G "Visual Studio 17 2022" -A x64
cmake --build out/build/vs2022 --config Release --parallel 4
ctest --test-dir out/build/vs2022 -C Release -R "^(core|desktop_model|receiver_lifecycle|control_protocol)$" --output-on-failure
```

Test tự động mặc định không mở nguồn Bluetooth hoặc phát âm thanh nghe được. Test stream thật tùy chọn: `tools/test.ps1 -RunMutedAudio`.

## Tài liệu

- [Hướng dẫn đầy đủ](README.vi.md)
- [Kiến trúc và kế hoạch](IMPLEMENTATION_PLAN.vi.md)
- [Danh sách chức năng](FEATURES.vi.md) · [Kế hoạch kiểm thử](TEST_PLAN.vi.md)
- [Giao thức backend](docs/PROTOCOL.md) · [Rà soát 0.2.1](docs/AUDIT_0.2.1.vi.md)
- [Dependency và giấy phép bên thứ ba](THIRD_PARTY.md)

Không đưa bản build, credential, profile cá nhân hoặc snapshot chứa ID thiết bị vào Git.
