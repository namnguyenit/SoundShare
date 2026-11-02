**Trạng thái core/backend — 02/11/2026**

Đã triển khai C++20 và build Release x64 trực tiếp trên Windows 11 Pro 25H2 build 26200.9711, MSVC 19.29, Windows SDK 19041. Tài liệu này ghi lại kiểm chứng backend ban đầu. Bản 0.2.0 đã bổ sung [giao diện desktop và kiểm chứng riêng](DESKTOP_STATUS.vi.md); driver riêng chưa có.

**Đã kiểm chứng**

| Hạng mục | Kết quả thực tế | Giới hạn của bằng chứng |
| --- | --- | --- |
| Build Windows | Worker, CLI, core và backend library biên dịch/link thành công | Không chứng minh mọi driver/thiết bị tương thích |
| Core | 10 nhóm test đạt: queue wrap/full/concurrency, ramp, clock, drift, SRC, fan-out, priming/recovery, mixer | Mô phỏng và tín hiệu tổng hợp |
| Drift | Bài mô phỏng 1 giờ với clock nhanh/chậm 100 ppm đạt | Không phải 1 giờ nghe Bluetooth thật |
| Memory instrumentation | 10 nhóm core test đạt dưới MSVC AddressSanitizer, cấu hình CRT static | Không thay cho race detector; dependency libsamplerate không được instrument toàn bộ trong lượt này |
| Protocol/persistence | Test validate, giới hạn message/depth, ID/version, feedback guard, Unicode config, trạng thái stop và named-pipe lifecycle đạt | Không mở receiver hoặc phát âm thanh nghe được |
| Windows inventory | Có các đầu ra Realtek Headphones, Realtek Speakers và FxSound; receiver list có Redmi Note 11 Pro và FPT Play Box+ | Là kết quả liệt kê, chưa mở nguồn để truyền |
| WASAPI probe | Khởi tạo render và loopback trên 3 đầu ra hiện có: 6 lượt đều đạt | Probe không gọi Start |
| Windows stream smoke | Chạy 30 giây: loopback FxSound → 2 đầu ra Realtek được mute; tất cả stream đạt trạng thái running | Nguồn ở trạng thái idle, engine cấp silence có thống kê riêng; không phải test nội dung nhạc hoặc Bluetooth |
| Counter trong lượt mute | Hai nhánh không ghi nhận dropped frames, underrun hoặc recovery trong thời gian đo | Không suy ra không mất gói vô tuyến |
| Lifecycle | Đổi delay/gain, remove/add một nhánh, stop và 3 chu kỳ start/stop thành công | Chưa thay bài 100 chu kỳ và chạy lâu theo kế hoạch |

Log kiểm thử đầy đủ tại `test-results/windows-smoke.json` trên máy phát triển; không đóng gói bản ghi âm vì không có ghi nội dung PCM.

**Cần kiểm thử bằng thiết bị thật trước khi công bố hỗ trợ**

- Mở receiver rồi phát media thực từ điện thoại và TV box, từng nguồn và đồng thời.
- Kiểm chứng receiver thực sự phát qua bridge đã chọn và ứng dụng thu đúng hỗn hợp.
- Tai nghe Bluetooth xuất hiện như endpoint hoạt động; thử 1 rồi 2 tai nghe khi PC đang nhận Bluetooth.
- Đo âm thanh tới từng tai nghe, gián đoạn và delay; chạy 60 phút/2 giờ/8 giờ theo ma trận.
- Mất sóng, HFP/microphone, sleep/resume và format invalidation trên driver thực.
- So sánh cùng điều kiện với Nahimic; đo CPU/RAM trong cấu hình mục tiêu có media thật.

**Phạm vi mã hiện tại**

Một bus capture được nhân bản qua các nhánh xử lý độc lập. Phần lõi có mixer nhưng receiver Windows không được giả định cung cấp PCM riêng theo điện thoại. Các cờ acoustic/playing/per-source/simultaneous verification không tự chuyển sang true vì API kết nối báo Opened.

Có retry và phục hồi stream hữu hạn, gain ramp, bù drift và offset; chưa có hiệu chỉnh acoustic tự động, ứng dụng giám sát tự restart worker hoặc hoàn nguyên Windows default endpoint. Diagnostics là snapshot và danh sách lỗi gần đây hữu hạn, chưa phải hệ thống ghi timeline mọi sự kiện ra tệp liên tục.

**Tái hiện các kiểm tra đã chạy**

```powershell
.\tools\build.ps1
.\tools\test.ps1
.\tools\test.ps1 -RunMutedAudio
```

Test sanitizer riêng, dùng CRT static để tương thích AddressSanitizer của MSVC 2019 trên máy này:

```powershell
cmake -S . -B out/build/asan -G "Visual Studio 16 2019" -A x64 -DSOUNDSHARE_BUILD_WINDOWS=OFF -DSOUNDSHARE_SANITIZE=ON -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
cmake --build out/build/asan --config Release --target soundshare_core_tests
.\out\build\asan\Release\soundshare_core_tests.exe
```

Không có thay đổi default endpoint, cài driver, mở receiver hoặc kết nối media của người dùng trong các lượt kiểm tra đã chạy.
