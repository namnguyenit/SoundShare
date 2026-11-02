**SoundShare — danh sách chức năng và phạm vi**

Ngày: 02/11/2026. Đây là danh sách chức năng toàn sản phẩm. Core/backend và giao diện Win32 đã có mã và kiểm thử; xem [trạng thái giao diện 0.2.0](docs/DESKTOP_STATUS.vi.md) và [backend](docs/BACKEND_STATUS.vi.md), không coi toàn bộ danh sách là đã hoàn thành.

[Kế hoạch triển khai](IMPLEMENTATION_PLAN.vi.md) · [Kế hoạch kiểm thử](TEST_PLAN.vi.md).

**Các mức ưu tiên**

- **Bắt buộc:** phải có trong bản dùng thử có thể sử dụng hằng ngày.
- **Mục tiêu phần cứng:** phải kiểm chứng sớm; chỉ công bố sau khi thử đạt, không mặc định loại bỏ khỏi yêu cầu.
- **Tiếp theo:** mở rộng sau khi đường âm thanh cơ bản ổn định.

**A. Nhận âm thanh từ điện thoại và TV box**

| ID | Chức năng | Mức | Điều kiện/hành vi |
| --- | --- | --- | --- |
| IN01 | Liệt kê nguồn Bluetooth có thể nhận media | Bắt buộc | Dùng khả năng receiver Windows phát hiện; không coi mọi thiết bị Bluetooth là nguồn âm thanh |
| IN02 | Hướng dẫn ghép đôi trong Windows | Bắt buộc | Chọn PC làm thiết bị phát âm thanh trên điện thoại/TV box; thông báo khi firmware không có chức năng đó |
| IN03 | Cho phép, mở, dừng nhận từng nguồn | Bắt buộc | Trạng thái và lỗi theo kết quả hệ điều hành; thao tác lặp không tạo kết nối trùng |
| IN04 | Ghi nhớ nhiều nguồn, chọn nguồn đang nghe | Bắt buộc | Nhớ thiết bị không đồng nghĩa tự mở nhận khi người dùng chưa bắt đầu phiên |
| IN05 | Điện thoại và TV box cùng phát vào PC | Mục tiêu phần cứng | Xác minh bằng hai nội dung phân biệt cùng có trong đầu ra |
| IN06 | Phát hỗn hợp hai nguồn ra các đầu ra đã chọn | Mục tiêu phần cứng | Có thể dùng hỗn hợp Windows đã trộn nếu không có PCM riêng |
| IN07 | Volume/mute riêng từng nguồn | Có điều kiện, tiếp theo | Chỉ có khi tìm được PCM hoặc điều khiển riêng ổn định; không dùng volume hỗn hợp để giả lập điều khiển nguồn |
| IN08 | Tự kết nối lại trong phiên được bật | Bắt buộc | Backoff có giới hạn; không coi pause/im lặng là lỗi; không giành lại nguồn ngoài phiên |
| IN09 | Hiển thị trạng thái nguồn chính xác | Bắt buộc | Phân biệt cho phép nhận, kết nối đã mở và bằng chứng đang có âm thanh; không suy ra hoạt động riêng từ meter hỗn hợp |

**B. Chia sẻ âm thanh và chọn đầu ra**

| ID | Chức năng | Mức | Điều kiện/hành vi |
| --- | --- | --- | --- |
| OUT01 | Liệt kê đầu ra dây, USB, HDMI và Bluetooth | Bắt buộc | Windows phải có endpoint phát có thể mở; tên giống nhau vẫn phân biệt được |
| OUT02 | Chọn nhiều đầu ra cùng lúc | Bắt buộc | Mục tiêu đầu tiên hai tai nghe Bluetooth, có thể thêm dây/USB; giới hạn theo topology đã đạt |
| OUT03 | Cùng nội dung trên mọi đầu ra được chọn | Bắt buộc | Mọi nhánh lấy từ cùng bus; không rơi mất nhánh mà UI vẫn báo đang phát |
| OUT04 | Thêm/bỏ đầu ra khi đang chạy | Bắt buộc | Không restart các nhánh đang tốt; nhánh mới nạp đệm và vào âm lượng êm |
| OUT05 | Volume/mute riêng đầu ra và master | Bắt buộc | Gain của SoundShare có ramp; không tăng quá 100% trong bản đầu |
| OUT06 | Chia sẻ âm thanh đang phát trên PC | Bắt buộc | Qua route ảo đã cấu hình; có thể gồm cả thông báo hệ thống khi chung bus |
| OUT07 | Chế độ chỉ phát loa chính | Bắt buộc | Giữ đường nghe bình thường khi không muốn chia sẻ; phân biệt với dừng nhận và thoát |
| OUT08 | Nhớ nhóm thiết bị và cấu hình | Bắt buộc | Thiết bị tạm vắng được giữ trong danh sách mong muốn, không chặn các thiết bị còn lại |
| OUT09 | Định tuyến từng nguồn tới từng nhóm loa | Tiếp theo, có điều kiện | Phải có đường nguồn độc lập; không làm được chỉ từ bus đã trộn |
| OUT10 | Chọn cặp kênh của interface nhiều kênh | Tiếp theo | “Đầu ra” bản đầu là endpoint Windows; chưa mặc định có channel matrix ASIO/surround |

**C. Độ mượt, đồng bộ và chất lượng**

| ID | Chức năng | Mức | Điều kiện/hành vi |
| --- | --- | --- | --- |
| AUD01 | Bộ đệm độc lập từng nhánh | Bắt buộc | Queue hữu hạn; nhánh chậm/lỗi không chặn mọi đầu ra |
| AUD02 | Chọn cấu hình đệm theo kết quả thử | Bắt buộc | Mặc định ưu tiên liên tục; không quảng bá độ trễ thấp nhất |
| AUD03 | Bù lệch tốc độ phát theo thời gian | Bắt buộc | Thay tỷ lệ lấy mẫu nhỏ, có giới hạn và lọc nhiễu; không cắt/lặp khối định kỳ |
| AUD04 | Chỉnh delay riêng từng đầu ra | Bắt buộc | Có nghe thử, reset và lưu; offset chỉ làm chậm thêm |
| AUD05 | Khởi động, thay volume và dừng có ramp | Bắt buộc | Không tạo tiếng click bởi thay đổi gain đột ngột |
| AUD06 | Chuyển sample rate và số kênh phù hợp | Bắt buộc | Bus stereo; format endpoint khác được đổi có chủ đích; không nhầm L/R |
| AUD07 | Bảo vệ mức âm khi trộn | Bắt buộc nếu có nguồn riêng | Gain staging và bảo vệ sample peak chung; đo latency của phần xử lý thêm |
| AUD08 | Đo độ lệch bằng microphone và gợi ý delay | Tiếp theo | Cần tín hiệu thử, môi trường đo và kiểm chứng độ tin cậy |
| AUD09 | Cấu hình ưu tiên trễ thấp | Tiếp theo | Chỉ đưa vào sau khi cấu hình ổn định đạt; hiển thị đánh đổi |
| AUD10 | EQ và hiệu ứng âm thanh | Tiếp theo | Không đưa vào đường baseline để tránh khó tìm nguyên nhân giật/ngắt |

**D. Phục hồi và vận hành**

| ID | Chức năng | Mức | Điều kiện/hành vi |
| --- | --- | --- | --- |
| OPS01 | Phục hồi riêng đầu ra mất kết nối | Bắt buộc | Các đầu ra còn tốt tiếp tục phát; không hứa thiết bị mất sóng vẫn có tiếng |
| OPS02 | Theo dõi format/profile thay đổi | Bắt buộc | Phát hiện invalidation; tái mở nhánh; đánh dấu delay cũ có thể không còn đúng |
| OPS03 | Ngủ/thức và bật/tắt Bluetooth | Bắt buộc | Kiểm kê lại; báo trạng thái thật; không vòng lặp mở lại liên tục |
| OPS04 | Engine tách khỏi giao diện | Bắt buộc | Cả hai tiến trình dùng C++20; UI treo/đóng cửa sổ không dừng phiên; engine crash có phục hồi riêng và ghi sự kiện |
| OPS05 | Tray và lưu trạng thái | Bắt buộc | Đóng cửa sổ/tray/thoát có ý nghĩa rõ; không để tiến trình phát âm thanh ngoài ý muốn |
| OPS06 | Tự chạy cùng Windows | Tiếp theo | Mặc định tắt; tự chạy UI không đồng nghĩa tự mở receiver |
| OPS07 | Hướng dẫn cấu hình và kết thúc route ảo | Bắt buộc | Đọc/xác minh default; hướng dẫn đổi trong Windows khi cần; không để gỡ app xong mất tiếng do route còn trỏ bridge |
| OPS08 | Đường phục hồi khi toàn bộ đầu ra biến mất | Bắt buộc | Tạm chờ hoặc dùng loa chính được người dùng cho phép; không tự phát ra loa bất kỳ |

**E. Giao diện và chẩn đoán**

| ID | Chức năng | Mức | Điều kiện/hành vi |
| --- | --- | --- | --- |
| UI01 | Màn hình Nguồn – Đầu ra – Phiên chia sẻ | Bắt buộc | Chọn checkbox thiết bị, bắt đầu phiên, thấy lỗi ở đúng thiết bị |
| UI02 | Meter âm thanh và tình trạng đầu ra | Bắt buộc | Meter hỗn hợp được ghi rõ; không hiển thị như số đo tiếng đã đến tai nghe |
| UI03 | Trang chẩn đoán nâng cao | Bắt buộc | Buffer, drift, discontinuity, lỗi API, topology và thời gian chạy |
| UI04 | Xuất gói chẩn đoán | Bắt buộc | Log có giới hạn; không chứa bản ghi âm media cá nhân mặc định |
| UI05 | Thông báo sự cố có cách xử lý | Bắt buộc | Phân biệt thiếu bridge, chưa chọn route, thiết bị vắng, kết nối thất bại và đầu ra không mở được |
| UI06 | Đánh dấu cấu hình đã kiểm thử | Bắt buộc | Trạng thái “đã thử” phải đi kèm hồ sơ thử, không suy từ Bluetooth version |

**F. Phân phối**

| ID | Chức năng | Mức | Điều kiện/hành vi |
| --- | --- | --- | --- |
| PKG01 | Gói Windows x64 dùng cá nhân | Bắt buộc | UI và worker cùng phiên bản, runtime/dependency đủ, dùng được với bridge cài riêng |
| PKG02 | Nâng cấp/gỡ ứng dụng có hướng dẫn route | Bắt buộc | Giữ cấu hình hợp lệ hoặc chuyển schema; không gỡ driver bên thứ ba do ứng dụng khác dùng |
| PKG03 | Driver SoundShare riêng | Tiếp theo, hạng mục lớn riêng | Cần transport PCM thật, ký driver, kiểm thử hệ thống và quy trình phân phối |
| PKG04 | Windows/thiết bị khác | Tiếp theo | Có ma trận tương thích riêng trước khi công bố |

**Những điều không cam kết trong bản đầu**

Không cam kết không mất gói vô tuyến, số tai nghe Bluetooth không giới hạn, codec LDAC/aptX cụ thể, tự chọn codec/bitrate bằng WASAPI, thời gian trễ cố định trên mọi loa hoặc độ khớp hình–tiếng của TV box. Không tự xử lý cuộc gọi HFP, microphone headset, thu âm người dùng, phát qua Internet hay ứng dụng gửi trên Android/iOS trong phạm vi ban đầu.

LE Audio tận dụng khi Windows và thiết bị thực sự hỗ trợ; không coi hỗ trợ Bluetooth 5.x là đủ. Gửi qua LAN là nhánh tương lai nếu cần nguồn độc lập hoặc phần cứng không đạt, chưa tự thay thế yêu cầu nhận Bluetooth.

**Luồng sử dụng dự kiến**

Lần đầu: kiểm tra máy và bridge → hướng dẫn ghép đôi nguồn → chọn loa/tai nghe → xác minh đường âm thanh → lưu nhóm. Các lần sau: mở ứng dụng → bật nguồn muốn nhận → chọn nhóm đầu ra → bắt đầu chia sẻ. Cấu hình hai nguồn đồng thời chỉ xuất hiện như khả năng được hỗ trợ khi phép kiểm chứng G2 đã đạt; nếu chưa đạt, hiển thị rõ giới hạn đó.
