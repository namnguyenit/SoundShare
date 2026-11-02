# Rà soát hệ thống SoundShare 0.2.1

Ngày: 02/11/2026. Phạm vi: core PCM/SRC/queue, WASAPI, receiver, IPC/protocol, model/giao diện, build/test và gói Windows. Đây là kết quả kiểm tra mã và các lượt test được nêu dưới đây, không phải chứng nhận mọi cấu hình phần cứng.

## Các vấn đề đã sửa

| Vấn đề trước sửa | Thay đổi |
| --- | --- |
| Gộp một lệnh chỉnh âm lượng mới vào vị trí lệnh cũ trước ranh giới stop/start | Hàng đợi chỉ gộp trong cùng đoạn lệnh; có test giữ đúng thứ tự và giá trị cuối |
| Worker thay đổi nhưng còn lệnh cũ và trạng thái UI của phiên trước | Worker công bố instance ID; client nhận diện thay đổi, bỏ lệnh cũ, UI trở về trạng thái quan sát mới; không tự mở receiver hoặc phát lại |
| Thoát phải chờ các lệnh mở nguồn/poll đang xếp hàng | Bỏ các thao tác chờ không còn cần thiết, giữ thao tác lưu cấu hình trước exit; vẫn cho phép thử lại nếu exit lỗi |
| Bật rồi tắt nhanh đầu ra trước khi snapshot kịp cập nhật có thể bỏ sót remove | Gửi remove theo ý định người dùng; remove idempotent; bỏ qua thay đổi checkbox không đổi giá trị |
| Đổi lựa chọn trong lúc start/stop hoặc đọc profile tạo trạng thái bất nhất | Khóa điều khiển phù hợp trong chuyển phiên/nhập profile; chặn phím tắt vượt qua trạng thái khóa |
| Nạp cấu hình chỉ chứa một đầu ra làm biến mất các đầu ra khác đang kết nối | Hợp nhất profile với inventory đang có, giữ các thiết bị chưa được chọn |
| Giao diện không gắn được vào phiên đã bỏ đầu ra cuối cùng | Cho phép tiếp nhận snapshot đang chạy có 0 đầu ra; vẫn từ chối nhập cấu hình 0 đầu ra để bắt đầu phiên |
| Một NaN/Infinity đi vào bộ lọc SRC có thể làm các khối sau tiếp tục lỗi | Làm sạch scratch trước SRC, chặn biên độ cực đoan; test xác nhận tín hiệu sạch trở lại bình thường |
| Peak cũ còn hiển thị trong khi nhánh đang prime | Đặt peak về 0 ở đầu mỗi lượt render |
| Receiver giữ khóa metadata trong khi chờ Start/Open tới hàng chục giây | Mở/khôi phục trên luồng nền, chờ ngoài khóa; status/close tiếp tục hoạt động; hủy tác vụ đang chờ bằng event |
| Hai client cùng thấy pipe sẵn sàng rồi tranh quyền mở gây lỗi ngẫu nhiên | Thử lại bước mở khi pipe vừa bị client khác chiếm, giữ thời hạn và hủy tác vụ |
| Lưu cấu hình vừa load nhưng chưa start lại lấy cấu hình engine rỗng | `config.save` dùng cấu hình staged khi chưa có phiên active |
| Stop nhiều đầu ra lần lượt cộng dồn thời gian fade/drain | Phát tín hiệu dừng tới tất cả nhánh trước khi join |
| Ô delay có thể đọc một phần chuỗi dán không hợp lệ | Kiểm tra toàn bộ ký tự và khoảng 0–1000 ms trước khi áp dụng |

Giữ đường render không cấp phát scratch mới trong lượt render, không khóa metadata receiver trên đường âm thanh và không gửi PCM qua pipe điều khiển. Không thay thuật toán drift hoặc giảm đệm khi chưa có số đo chứng minh cần thiết.

## Kiểm chứng

- Release x64: bốn bộ CTest đạt — 11 nhóm core, 11 nhóm model/queue, 4 nhóm receiver/transport và integration protocol.
- 11 nhóm core đạt dưới MSVC AddressSanitizer với CRT static. Dependency không được instrument toàn bộ; đây không phải công cụ phát hiện mọi data race.
- Receiver giả lập: open/status không chờ kết nối hoàn tất; đóng hủy tác vụ; không mở trùng; lỗi ban đầu → retry thành công; mất kết nối → khôi phục; destructor hủy và giải phóng; chờ pipe có thể hủy. Không mở receiver Bluetooth thật trong các test này.
- Protocol: 32 yêu cầu status từ tám client đồng thời, lưu cấu hình Unicode/staged, validate, vòng đời pipe và shutdown.
- Windows thật: sáu probe render/loopback trên ba endpoint; loopback FxSound → hai đầu ra Realtek, master/gain bằng 0, chạy 30,01 giây. Cả hai stream running, không ghi nhận underrun, dropped frames, recovery hoặc timestamp error trong lượt đo. Nguồn idle; dữ liệu được cấp là silence có thống kê riêng.
- Worker CPU trong lượt 30 giây: 1,77% của một lõi theo tổng kernel + user time chia thời gian thực. Đây là một số đo trên máy hiện tại, không phải cam kết hoặc so sánh tăng tốc với bản cũ.
- 100 chu kỳ start/stop thành công, có thử đổi delay và remove/add nhánh. Không đổi thiết bị mặc định, không mở nguồn Bluetooth, không ghi PCM vào tệp.
- Giao diện khởi động được worker 0.2.1; dừng worker qua giao thức rồi xác nhận GUI tạo worker có instance ID mới, vẫn idle và không tự phát lại.
- Nhập cấu hình backend chỉ có một đầu ra qua hộp thoại GUI: vẫn thấy đủ ba thiết bị đang kết nối. Từ GUI, start ở master 0, thêm rồi bỏ đầu ra thứ hai; snapshot backend xác nhận còn đúng một nhánh running, không underrun/drop/recovery trên nhánh giữ lại.

Snapshot máy phát triển: `test-results/windows-audit-0.2.1.json` và `test-results/desktop-audit-0.2.1.json`. Gói phát hành không chứa các ID thiết bị này.

## Giới hạn còn lại

Chưa kiểm chứng media thật từ điện thoại + TV box đồng thời và hai tai nghe Bluetooth; chưa kiểm tra RF congestion, ngắt/mất sóng thực, độ lệch âm học, chạy qua đêm, sleep/resume và nhiều mức DPI/màn hình. Không có driver ảo riêng; vẫn cần kiểm tra định tuyến Windows/bridge. Bộ đếm WASAPI không đo mất gói Bluetooth. Các mục phần cứng theo [kế hoạch test](../TEST_PLAN.vi.md) vẫn cần thực hiện trước khi công bố khả năng hỗ trợ.

Receiver dùng đặc tính Agile/Both của [AudioPlaybackConnection](https://learn.microsoft.com/en-us/uwp/api/windows.media.audio.audioplaybackconnection?view=winrt-26100); cơ chế hủy đối chiếu [tài liệu concurrency C++/WinRT](https://learn.microsoft.com/en-us/windows/apps/develop/cpp-winrt/concurrency-2). Yêu cầu hủy là hợp tác với Windows, không thể cam kết thời gian đáp ứng của mọi driver bên ngoài.
