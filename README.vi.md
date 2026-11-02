**SoundShare 0.2.1 — core và giao diện C++20 cho Windows**

Đã có giao diện Win32 native, engine WASAPI chạy riêng và công cụ dòng lệnh. [Tổng thiết kế](IMPLEMENTATION_PLAN.vi.md) · [Giao thức backend](docs/PROTOCOL.md) · [Kiểm chứng giao diện](docs/DESKTOP_STATUS.vi.md) · [Kiểm chứng backend](docs/BACKEND_STATUS.vi.md).

Bản 0.2.1 sửa thứ tự lệnh, các tình huống khởi động lại/đổi thiết bị, mở nhận Bluetooth trên luồng nền và xử lý mẫu PCM lỗi. Xem [báo cáo rà soát và số đo](docs/AUDIT_0.2.1.vi.md).

**Mở ứng dụng**

Chạy `out\build\windows-x64\Release\SoundShare.Desktop.exe`, hoặc giải nén gói `out\SoundShare-windows-x64.zip` rồi chạy `bin\SoundShare.Desktop.exe`. Giữ `SoundShare.Worker.exe` cạnh giao diện; chương trình tự mở worker và không yêu cầu quyền quản trị. Máy cần Microsoft Visual C++ Runtime x64 tương thích VS 2019 trở lên. Chưa có installer hoặc driver âm thanh ảo riêng trong gói này.

1. Chọn nguồn âm thanh PC (loopback) hoặc một đầu vào Recording cụ thể. Với virtual cable, chọn thêm đúng phía Playback của bridge để chặn vòng lặp.
2. Đánh dấu loa/tai nghe đầu ra. Chọn một hàng để chỉnh âm lượng, mute và độ trễ của nhánh đó.
3. Chọn mức đệm và âm lượng chung, nhấn **Bắt đầu chia sẻ**. Có thể thêm/bớt đầu ra trong khi chạy.
4. Với điện thoại/TV box đã ghép đôi, chọn nguồn trong danh sách và nhấn **Nhận âm thanh** sau khi bắt đầu phiên. Cần kiểm tra đường phát Windows/bridge như phần dưới; mở kết nối chưa đồng nghĩa đang nhận nhạc.

Trang **Hướng dẫn** có lưu/mở cấu hình, cài đặt âm thanh/Bluetooth và thoát. Trang **Chẩn đoán** có trạng thái, bộ đếm và xuất JSON. Ctrl+S/Ctrl+O để lưu/mở, F5 làm mới, Ctrl+Q thoát. Đóng cửa sổ lúc đang chia sẻ sẽ thu về khay. Thoát rõ ràng sẽ dừng phiên và đóng receiver.

Cấu hình được nhớ tại `%LOCALAPPDATA%\SoundShare\desktop.json`; lần mở sau không tự phát hoặc mở receiver. Không đổi default endpoint hoặc cài dịch vụ vào máy.

**Backend hiện có**

- WASAPI shared capture/loopback và fan-out tới 1–8 endpoint được chọn.
- PCM nội bộ float32 stereo 48 kHz; Windows thực hiện chuyển format endpoint qua cờ shared-mode tương ứng.
- Queue SPSC hai giây độc lập từng đầu ra, cấp sẵn scratch và trạng thái resampler; nhánh chậm không chặn các nhánh khác.
- libsamplerate streaming, có tính dự trữ bên trong resampler; điều khiển drift có giới hạn kết hợp mức đầy queue và clock hợp lệ.
- Volume/master/mute có ramp, delay từng đầu ra, thêm/bỏ đầu ra, re-prime và retry hữu hạn khi stream lỗi.
- C++/WinRT receiver: liệt kê, mở, đóng nhiều handle và tùy chọn kết nối lại. Chưa xác nhận hai nguồn cùng truyền trên phần cứng thật.
- Named pipe cục bộ có phân quyền, giao thức JSON v1, chế độ stdio để test/tích hợp giao diện.
- Validate cấu hình, lưu/đọc JSON Unicode, xuất snapshot chẩn đoán và giữ các lỗi gần đây. Không ghi âm media mặc định.

Engine chỉ thu một bus/endpoint ở bản này. Có hàm mixer trong core để kiểm thử/mở rộng, nhưng không có API PCM riêng cho từng điện thoại; không cung cấp thanh chỉnh nguồn giả khi Windows đã trộn chúng.

**Build trên máy hiện tại**

Cần MSVC hỗ trợ C++20, Windows SDK 19041 trở lên, CMake 3.20 trở lên; Python 3 chỉ dùng cho integration test. Preset hiện tại dùng Visual Studio Build Tools 2019 x64 đã có trên máy. Các gói được khóa phiên bản/hash và tải khi configure lần đầu; không sửa SDK hoặc cài compiler toàn hệ thống.

```powershell
cd D:\soundshare
.\tools\build.ps1
.\tools\test.ps1
# Build, test và đóng gói ZIP:
.\tools\package.ps1
```

Chương trình nằm trong `out\build\windows-x64\Release`. Nếu dùng VS 2022, có thể tạo build riêng:

```powershell
cmake -S . -B out/build/vs2022 -G "Visual Studio 17 2022" -A x64
cmake --build out/build/vs2022 --config Release --parallel 4
ctest --test-dir out/build/vs2022 -C Release -R "^(core|desktop_model|receiver_lifecycle|control_protocol)$" --output-on-failure
```

CMake tự tạo projection C++/WinRT mới từ metadata Windows SDK, để giữ C++20 khi SDK cũ kèm projection coroutine cũ. Với generator khác Visual Studio, chỉ rõ `SOUNDSHARE_WINMD` tới tệp `UnionMetadata/<version>/Windows.winmd` của SDK.

**Chạy worker và xem thiết bị**

Mở worker trong một cửa sổ console:

```powershell
.\out\build\windows-x64\Release\SoundShare.Worker.exe --pipe
```

Trong cửa sổ khác:

```powershell
.\out\build\windows-x64\Release\SoundShare.Ctl.exe hello
.\out\build\windows-x64\Release\SoundShare.Ctl.exe inventory
.\out\build\windows-x64\Release\SoundShare.Ctl.exe request '{"protocol":1,"id":1,"method":"receiver.list"}'
.\out\build\windows-x64\Release\SoundShare.Ctl.exe status
```

Windows PowerShell 5.1 có cách truyền dấu ngoặc kép cho executable khác PowerShell 7. Để tránh lỗi escaping, có thể viết JSON vào tệp UTF-8 rồi dùng `request-file`, như các mẫu trong `examples`.

**Đưa điện thoại/TV box qua backend**

1. Có một virtual cable/bridge được cài riêng. Chọn phía Playback của bridge làm đường nhận âm thanh trong Windows; xác minh receiver thực sự đi theo đường đó.
2. Lấy ID qua `inventory`, điền phía Recording của bridge vào `capture.id`, phía Playback vào `capture.bridge_render_id` và các đầu ra vật lý vào `outputs` trong `examples\bridge-session.json`.
3. Nạp cấu hình, bắt đầu engine rồi mở receiver từng nguồn bằng ID từ `receiver.list`. Trạng thái `opened` chưa chứng minh đang nhận media.
4. Phát media trên thiết bị nguồn, theo dõi `status`, kiểm tra bằng tai và phép đo. Chỉnh delay/gain nếu cần; bắt đầu ở âm lượng vừa phải.

```powershell
.\out\build\windows-x64\Release\SoundShare.Ctl.exe request-file examples\load-config.request.json
.\out\build\windows-x64\Release\SoundShare.Ctl.exe request-file examples\start-session.request.json
# Sửa ID nguồn trong tệp mẫu trước khi gọi:
.\out\build\windows-x64\Release\SoundShare.Ctl.exe request-file examples\open-receiver.request.json
```

Backend không tự đổi default endpoint, ghép đôi thiết bị hoặc cài driver. `bridge_render_id` ngăn chọn nhầm phía phát của bridge làm đích, nhưng không thể tự biết mọi đường loop do phần mềm ngoài tạo.

**Dừng và kết thúc**

```powershell
.\out\build\windows-x64\Release\SoundShare.Ctl.exe request '{"protocol":1,"id":2,"method":"session.stop"}'
.\out\build\windows-x64\Release\SoundShare.Ctl.exe shutdown
```

Nếu đã chọn đầu ra ảo trong Windows, đưa default trở về loa/tai nghe vật lý khi không dùng backend. Chương trình không sửa lại default bằng API không được tài liệu hóa. Stop/shutdown đóng receiver và giải phóng các stream; không có dịch vụ tự chạy ngầm được cài vào máy.

**Kiểm thử**

Test tự động gồm concurrency của queue, wrap/tràn/thiếu dữ liệu, ramp, mixer, streaming SRC, sai timestamp, mô phỏng clock lệch một giờ, validate protocol, lưu cấu hình Unicode, vòng đời named pipe và model giao diện/profile. Có bốn bộ CTest: core (11 nhóm), desktop model/hàng đợi lệnh (11 nhóm), receiver/transport lifecycle (4 nhóm mô phỏng) và integration protocol (bao gồm 32 yêu cầu từ nhiều client). Test mô phỏng receiver không xác nhận media Bluetooth thật.

Test Windows tùy chọn dưới đây khởi tạo stream thật và chạy hai đầu ra được mute; không đổi default, không mở receiver, không ghi nội dung PCM. Đây là test vòng đời stream, chưa phải test âm thanh Bluetooth hoặc nghe nhạc.

```powershell
.\tools\test.ps1 -RunMutedAudio
```

Các bài nghe/chạy lâu, mất sóng và hai nguồn → hai tai nghe theo [TEST_PLAN](TEST_PLAN.vi.md) cần thiết bị thật. Chưa tuyên bố đạt độ mượt hoặc đồng bộ âm học từ test mute.

**Phạm vi chưa xác nhận**

Hai nguồn Bluetooth cùng truyền; tách PCM từng nguồn; hai tai nghe Bluetooth cùng hoạt động khi PC đang nhận; hiệu chỉnh âm học; ngủ/thức/mất sóng dài; chứng nhận mức CPU/RAM và chạy lâu. Driver riêng và installer chưa được triển khai. [Dependency và giấy phép](THIRD_PARTY.md).
