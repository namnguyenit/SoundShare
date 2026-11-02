# SoundShare Desktop 0.2.0 — trạng thái thực thi

Ngày kiểm tra: 02/11/2026, Windows 11 Pro x64 trên máy hiện tại.

Tài liệu này ghi lại bản giao diện đầu tiên 0.2.0. Bản hiện tại 0.2.1 có sửa lỗi và kiểm chứng bổ sung tại [báo cáo rà soát](AUDIT_0.2.1.vi.md).

## Giao diện và cơ chế

Ứng dụng C++20/Win32 native, không dùng trình duyệt nhúng. Giao diện và WASAPI chạy trong hai tiến trình riêng. Giao diện tự tìm hoặc khởi động `SoundShare.Worker.exe` đặt cạnh nó, trao đổi qua named pipe cục bộ có phân quyền. Lệnh, lưu tệp và truy vấn thiết bị chạy trên luồng nền; thay đổi thanh âm lượng được gộp khi chờ gửi. Đường âm thanh không phụ thuộc vòng lặp vẽ giao diện.

Ba trang: Chia sẻ, Chẩn đoán, Hướng dẫn. Có nhận diện DPI theo màn hình, bàn phím, điều khiển chuẩn Windows và khay hệ thống. Các trang và thông báo chính dùng tiếng Việt.

Đã có:

- Liệt kê endpoint Playback/Recording và các thiết bị nguồn A2DP Windows cung cấp.
- Chọn loopback âm thanh PC hoặc một endpoint Recording cụ thể; chọn phía Playback của bridge để chặn vòng lặp trực tiếp.
- Chọn nhiều đầu ra; thêm/bớt nhánh trong lúc chạy, chọn chỉ một đầu ra, gain/mute/delay từng nhánh và âm lượng chung.
- Ba mức dự trữ đệm ứng dụng 80/120/240 ms; đây không phải tổng độ trễ đầu cuối.
- Mở/đóng receiver từng nguồn và bật kết nối lại có giới hạn. Nút mở nhận yêu cầu bắt đầu phiên trước; không tự mở nguồn khi khởi động.
- Theo dõi trạng thái stream, peak phần mềm, queue, drift, underrun và lỗi. Dữ liệu chẩn đoán có thể đóng băng hoặc xuất JSON, không ghi nội dung âm thanh.
- Lưu/mở profile JSON; tự nhớ lựa chọn tại `%LOCALAPPDATA%\SoundShare\desktop.json`, ghi qua tệp tạm rồi thay thế. Lần mở sau không tự phát hoặc mở Bluetooth.
- Ctrl+S lưu, Ctrl+O mở, Ctrl+Q thoát, F5 làm mới. Đóng cửa sổ khi đang phát sẽ thu về khay; mở lại chương trình sẽ hiện cửa sổ cũ. Thoát rõ ràng sẽ dừng stream và receiver; chỉ tắt worker do chính giao diện khởi động.

Nguồn Recording phải được chọn rõ ràng để tránh vô tình thu microphone. Giới hạn tám nhánh là giới hạn phần mềm, không phải cam kết tám thiết bị Bluetooth hoạt động đồng thời.

## Đã kiểm tra

- Build MSVC Release x64; ba bộ CTest: core (10 nhóm), desktop_model (8 nhóm), control_protocol.
- Test model: cập nhật inventory, thiết bị mất kết nối, chặn feedback, chọn nguồn Recording rõ ràng, profile round-trip/rollback, validate gain/delay và đồng bộ trạng thái phiên.
- Test giao thức: JSON lỗi/giới hạn, cấu hình Unicode, `config.current`, inventory Windows thật, named pipe và shutdown; không mở receiver trong test này.
- Khởi động giao diện kéo theo worker; bắt đầu/dừng loopback FxSound → hai endpoint Realtek với master 0. Xác nhận cả hai stream running qua backend.
- Chỉnh delay 50 ms từ giao diện, bỏ/thêm nhánh và chọn chỉ một đầu ra khi phiên đang chạy; nhánh khỏe tiếp tục hoạt động.
- Chặn chọn loopback source làm đầu ra; các điều khiển nguồn khóa khi phiên đang chạy.
- Đóng cửa sổ lúc đang chạy giữ phiên trong khay; mở lại hiện một cửa sổ, không sinh engine thứ hai.
- Dừng, thoát bằng Ctrl+Q, mở lại: khôi phục lựa chọn và gain/delay nhưng phiên vẫn dừng. Lưu/mở profile và xuất chẩn đoán bằng hộp thoại Windows đã kiểm tra; JSON chẩn đoán không chứa bản ghi âm.
- Kiểm tra trực quan ba trang, hiển thị chẩn đoán nhiều dòng và trạng thái nút trên màn hình hiện tại.

Các snapshot chạy cục bộ nằm trong `test-results/`, không đưa vào gói phát hành vì có ID thiết bị của máy.

## Phạm vi cần thiết bị thật

Chưa xác nhận điện thoại và TV box cùng truyền media, hai tai nghe Bluetooth đồng thời, độ mượt nghe thực tế, mất sóng/ngủ-thức kéo dài, chạy liên tục nhiều giờ, độ trễ âm học và mức CPU/RAM chuẩn hóa. Không suy ra mất gói Bluetooth từ bộ đếm WASAPI.

Windows receiver hiện không đưa PCM riêng cho từng nguồn vào engine này. Engine đọc một endpoint/bus đã được Windows/bridge định tuyến. Trạng thái `opened` chỉ xác nhận mở kết nối, không xác nhận thiết bị đang phát. Driver âm thanh ảo riêng và installer chưa có trong bản này; cấu hình bridge/default trong Windows cần thực hiện thủ công và kiểm tra đường âm thanh.

Xem [README](../README.vi.md), [giao thức](PROTOCOL.md), [kiểm chứng backend](BACKEND_STATUS.vi.md) và [kế hoạch test thiết bị](../TEST_PLAN.vi.md).
