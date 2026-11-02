**SoundShare — kế hoạch kiểm thử và tiêu chí nghiệm thu**

Ngày: 02/11/2026. Đây là kế hoạch nghiệm thu toàn sản phẩm. Bản core/backend đã chạy một phần test tự động và WASAPI mute; xem [kết quả hiện tại](docs/BACKEND_STATUS.vi.md). Không coi toàn bộ 30 bài dưới đây là đã đạt. [Thiết kế và các mốc code](IMPLEMENTATION_PLAN.vi.md) · [Mã chức năng](FEATURES.vi.md).

**1. Nguyên tắc đánh giá**

Đo ba lớp riêng: engine cấp PCM có liên tục không; Windows/thiết bị có duy trì kết nối không; âm thanh thực sự tới tai nghe có bị ngắt, click hoặc lệch tăng dần không. Số đếm buffer của ứng dụng không thay cho phép đo đầu ra Bluetooth.

Ưu tiên nghiệm thu độ mượt. Mức delay đồng bộ được đo và báo rõ; độ trễ hình–tiếng của điện thoại/TV box là chỉ số riêng, không lấy độ lệch giữa hai tai nghe thay cho nó.

Test tự động dùng tín hiệu tổng hợp và thiết bị giả để chứng minh thuật toán. Test phần cứng cần điện thoại, TV box và tai nghe thật; có những bước người dùng phải thao tác trên thiết bị nguồn. Không coi test mô phỏng là kết quả tương thích Bluetooth.

**2. Chuẩn bị một phiên thử có thể lặp lại**

Ghi mã bản build, cấu hình engine, phiên bản Windows/driver, nguồn, đầu ra, codec nếu đọc được, format, buffer, delay, trạng thái hiệu ứng, khoảng cách và Wi-Fi band. Không điền codec phỏng đoán. Ghi lại route mặc định trước thử để kết thúc có thể trở về cấu hình sử dụng bình thường.

Baseline đầu tiên dùng thiết bị gần PC, khoảng 1–2 m, không vật cản đáng kể, Wi-Fi 5 GHz nếu có, nguồn điện và chế độ nguồn ổn định. Đây là điều kiện kiểm thử có kiểm soát, không phải lời hứa về khoảng cách hoạt động tối đa.

Tắt chia sẻ âm thanh của Nahimic và các router khác trong baseline SoundShare; giữ thông tin cấu hình để bật lại sau thử. FxSound/hiệu ứng được kiểm thử ở lượt tương thích riêng. Không thay driver giữa hai lượt A/B; nếu thay phải coi là bộ kết quả mới.

Đầu ra ảo PoC được cài từ nguồn chính thức theo giấy phép của nhà cung cấp. Chỉ cài khi bắt đầu thử thực tế; kế hoạch này không thực hiện cài đặt hoặc thay cấu hình máy.

**3. Tín hiệu và cách đo**

| Tín hiệu/công cụ | Dùng cho | Cách tránh kết luận sai |
| --- | --- | --- |
| Âm thử A và B có đặc trưng khác nhau | Chứng minh điện thoại/TV box cùng phát | Mỗi nguồn có nhịp/tần số riêng; thử A, B, A+B và pause từng nguồn |
| Stereo có marker L/R và các đoạn nói | Kiểm tra đổi kênh, mất kênh, downmix và chất lượng nghe | Kiểm tra riêng từng nhánh trước khi phát nhiều nhánh |
| Chuỗi xung/marker có khoảng cách biết trước | Phát hiện đoạn thiếu/lặp, đo lệch và drift | Không dùng ngưỡng “im lặng” trên bản nhạc có khoảng lặng tự nhiên |
| Nhạc/giọng nói thực tế | Đánh giá click, méo, mệt tai và hình–tiếng | Bổ sung cho số đo; không thay bài tín hiệu xác định |
| Log QPC, clock, queue, underrun/overrun | Tìm lỗi xử lý và lên lịch | Không gọi các chỉ số này là bộ đếm packet Bluetooth |
| Thu nhiều kênh bằng thiết bị ghi chung clock | Đo tiếng đến hai tai nghe và độ lệch | Mỗi microphone gần một tai nghe, giảm lẫn âm; hiệu chỉnh chênh đường đo |
| Loopback/capture trên PC | Kiểm tra PCM trước/giữa các bước | Không chứng minh dữ liệu đã qua radio tới tai nghe thành công |

Nếu không có bộ ghi nhiều kênh phù hợp, vẫn làm kiểm thử nghe và engine, nhưng kết quả đồng bộ âm học chỉ được ghi là đánh giá sơ bộ. Không công bố số lệch vài millisecond từ hai bản ghi riêng không có clock chung.

Độ trễ từ nguồn tới tai nghe chỉ được gọi là đo thực khi có điểm tham chiếu nguồn và đầu ra trên cùng hệ đo thời gian. Khi chưa có, ghi ước lượng đường PC và đánh giá hình–tiếng riêng, không cộng các latency API rồi coi là số đo toàn đường.

**4. Ma trận cấu hình thiết bị**

W = đầu ra dây hoặc USB; B1/B2/B3 = từng tai nghe Bluetooth thật được ghi model/firmware. P = điện thoại; T = TV box. Mỗi cấu hình cần thử thứ tự mở thiết bị khác nhau, không chỉ một lần tình cờ thành công.

| Mã | Nguồn vào | Đầu ra | Mục đích |
| --- | --- | --- | --- |
| M01 | Tín hiệu/file từ PC | W | Đường chuẩn cho engine, không tải radio |
| M02 | Tín hiệu/file từ PC | W + USB/HDMI khác | Hai clock vật lý, chuyển format |
| M03 | Tín hiệu/file từ PC | B1 | Đường chuẩn một đầu ra Bluetooth |
| M04 | Tín hiệu/file từ PC | B1 + B2 | Đường nhiều đầu ra để so Nahimic |
| M05 | P | W | Chứng minh receiver điện thoại |
| M06 | T | W | Chứng minh receiver TV box và ứng dụng media mục tiêu |
| M07 | P + T | W | Tách giới hạn hai nguồn khỏi tải phát Bluetooth |
| M08 | P | B1, rồi B1 + B2 | Tăng tải nhận/phát từng bước |
| M09 | T | B1, rồi B1 + B2 | Phân biệt vấn đề riêng TV box |
| M10 | P + T | B1 | Hai nguồn với một đích Bluetooth |
| M11 | P + T | B1 + B2 | Cấu hình mục tiêu chính: bốn luồng radio |
| M12 | P + T | B1 + B2 + W | Thêm đầu ra không dùng băng thông Bluetooth |
| M13 | P + T hoặc một nguồn | B1 + B2 + B3 | Khảo sát mở rộng; không là điều kiện mặc định bản đầu |

M07 thất bại thì chưa kết luận card hết băng thông phát: trước hết điều tra hỗ trợ hai nguồn ở Windows/driver. M07 đạt nhưng M11 lỗi cần tách thử M04, M08, M09 và M10 để khoanh vùng.

**5. Kiểm thử tự động của lõi xử lý**

| ID | Bài thử | Kết quả mong đợi | Chức năng |
| --- | --- | --- | --- |
| T01 | Ring buffer wrap-around, đầy/rỗng, đọc nhiều nhánh, bỏ một reader | Không ghi đè dữ liệu nhánh còn hợp lệ ngoài chính sách; không out-of-bounds; queue hữu hạn | AUD01, OUT03 |
| T02 | Source/endpoint 44,1/48/96 kHz, mono/stereo, đổi format | Tốc độ, số frame, thứ tự kênh đúng; xử lý format không hỗ trợ bằng lỗi rõ | AUD06 |
| T03 | Đổi tỷ lệ resample với trạng thái liên tục | Không reset history mỗi block; biên block không tạo click ngoài ngưỡng phân tích đã chuẩn hóa | AUD03 |
| T04 | Clock giả nhanh/chậm ±50/100/300 ppm, thêm nhiễu số đo | Controller đúng chiều, hội tụ, queue nằm vùng an toàn, ratio có clamp; không trôi không giới hạn | AUD03 |
| T05 | Clock/timestamp lỗi, ngừng tăng, nhảy sau reconnect | Bỏ số đo không tin cậy; chuyển phục hồi hữu hạn; không ratio cực đoan hoặc vòng lặp bận | OPS02 |
| T06 | Gain/mute/ramp, hai tín hiệu full-scale, marker L/R | Không tạo nhảy gain; không clipping sample do engine cộng nguồn; không NaN/Inf | AUD05–AUD07 |
| T07 | Thiếu dữ liệu ngắn/dài, queue đầy, nhánh đọc chậm | Ghi sự kiện; không bơm lại PCM cũ vô hạn; nhánh lỗi không chặn nhánh khỏe | AUD01, OPS01 |
| T08 | Start/stop lặp, nguồn pause, add/remove trong lúc open, lỗi IPC | State machine xác định, hủy lệnh an toàn, không leak thread/handle, không capture trùng | IN03, OUT04, OPS04 |

Các bài giả lập có thể chạy nhanh hơn thời gian thực để mô phỏng nhiều giờ; không thay bài clock/driver thực. Dùng kiểm tra đồng thời, memory diagnostics và stress producer/consumer cho native core ở bản debug phù hợp; tách kết quả timing của bản debug khỏi benchmark Release.

**6. Kiểm thử tích hợp receiver, routing và nhiều nguồn**

| ID | Các bước chính | Điều kiện đạt |
| --- | --- | --- |
| T09 | Liệt kê P/T; mở P vào W; pause/resume; đóng/mở 10 lần | Nhận đúng nguồn; không kết nối trùng; pause không kích retry; trạng thái hợp lệ |
| T10 | Lặp T09 với T, dùng từng ứng dụng media cần sử dụng | Ghi rõ ứng dụng/firmware nào phát được; không coi tên thiết bị là bằng chứng |
| T11 | P phát A, T phát B; mở P→T và T→P; pause từng nguồn | A và B cùng hiện diện khi yêu cầu cùng phát; nguồn thứ hai không thay thế nguồn thứ nhất; phân loại G4 A/B/C |
| T12 | Receiver → bridge Recording; so với đường trực tiếp | Đúng nội dung, không mất kênh; ghi latency thêm; không thu bridge hai lần |
| T13 | Chọn đầu ra ảo trong Windows, đổi default/role, mở nguồn trước/sau | Xác định receiver thực sự theo route nào; lỗi route được báo, không âm thầm phát sai đầu ra |
| T14 | Thử chọn bridge làm đầu ra, mở hai phiên, thay capture khi chạy | Ngăn feedback và route trùng; một phiên điều khiển có hiệu lực |
| T15 | PC audio cùng receiver trên cùng bridge; thử điều khiển từng nguồn | Nhận diện đúng hỗn hợp; nút riêng chỉ hoạt động nếu có control riêng đã chứng minh |

G2/G4 cần ít nhất 10 chu kỳ mở/phát với hai thứ tự kết nối và một phiên 30 phút M07 trước khi dùng kết quả để thiết kế giao diện nhiều nguồn. G3 cần đạt T12/T13 với receiver điện thoại lẫn TV box.

**7. Kiểm thử đồng bộ và phục hồi**

| ID | Kịch bản | Điều kiện đạt |
| --- | --- | --- |
| T16 | Phát marker qua M02/M04/M08/M11; chỉnh delay rồi đo ở đầu, giữa, cuối phiên | Offset có tác dụng đúng nhánh, không thay tốc độ nội dung; không có drift tăng đơn điệu không được kiểm soát |
| T17 | Thêm/bỏ B2 khi B1/W đang phát; lặp 20 lần | B1/W không restart; B2 join bằng ramp; không queue tăng mãi |
| T18 | Tắt/mở một tai nghe, ra khỏi vùng sóng rồi trở lại | Chỉ nhánh đó vào phục hồi; đầu ra còn tốt không ngắt do SoundShare restart toàn nhóm |
| T19 | Ngắt P, giữ T và ngược lại; pause lâu, nguồn im lặng 5 phút | Nguồn còn lại tiếp tục nếu Windows hỗ trợ; im lặng có chủ đích không bị coi là hỏng |
| T20 | Bật microphone/call trên headset; đổi format endpoint | Phát hiện HFP/format thay đổi nếu hệ thống báo; xử lý tái mở, ghi lại chất lượng và delay; không treo |
| T21 | Sleep/resume, Bluetooth off/on, thiết bị USB/HDMI biến mất | Có chuyển trạng thái rõ; mở lại các đầu ra được chọn còn khả dụng; không retry storm |
| T22 | Đóng UI, treo UI, kill worker riêng, IPC đứt | UI đóng/treo không giết engine; worker crash được ghi và phục hồi có giới hạn; không gọi gián đoạn này là “không ngắt” |

Thời gian phục hồi được đo từ lúc Windows báo endpoint/nguồn khả dụng, tách khỏi thời gian ghép đôi hoặc người dùng bật thiết bị. Mục tiêu kỹ thuật ban đầu: tái mở nhánh trong 10 giây khi endpoint đã sẵn sàng; receiver đo riêng vì có đàm phán với nguồn. Nếu driver không đạt, báo số đo và nguyên nhân, không sửa mốc thời gian bắt đầu để làm kết quả đẹp.

**8. Kiểm thử giao diện, cấu hình và máy sử dụng thật**

| ID | Kịch bản | Điều kiện đạt |
| --- | --- | --- |
| T23 | Người dùng mới hoàn thành hướng dẫn, nguồn/đầu ra tên giống nhau, bridge thiếu; UI Win32 ở các mức DPI và điều khiển bàn phím | Không chọn nhầm thiết bị; thông báo có bước xử lý; không yêu cầu hiểu API; không vỡ bố cục và thao tác chính truy cập được |
| T24 | Lưu profile, thiết bị vắng, đổi tên, restart app; cấu hình lỗi/phiên bản cũ | Giữ ID đúng; chỉ thiếu thiết bị không làm mất profile; fallback cấu hình có thông báo |
| T25 | Chỉ loa chính, dừng nhận, thoát, gỡ app; kiểm tra route Windows | Đúng hành vi mô tả; không để mất tiếng kéo dài vì default trỏ bridge không có worker |
| T26 | Installer x64 trên môi trường sạch, dependency MSVC/native, nâng cấp và uninstall | Chạy được không cần môi trường phát triển hoặc .NET; UI/worker/protocol khớp; không tự gỡ bridge bên thứ ba |

Tự động hóa UI chỉ cần các luồng quan trọng bằng dữ liệu/harness có kiểm soát. Phần ghép đôi và thao tác trên điện thoại/TV box được ghi là kiểm thử thủ công có hướng dẫn, không giả là đã tự động hoàn toàn.

**9. Test tải, chạy lâu và A/B**

| ID | Bài thử | Thời lượng/lượt dự kiến | Kết quả cần ghi |
| --- | --- | --- | --- |
| T27 | Baseline và chạy lâu | Smoke 5 phút mỗi topology; 60 phút baseline; 2 giờ cho M04/M08/M11/M12 nếu khả dụng; 8 giờ cho cấu hình phát hành chính | Giật/ngắt, queue, drift, CPU/RAM, handle/thread và thay đổi kết nối |
| T28 | Tải nền CPU/GPU/đĩa/mạng, đổi nguồn điện/chế độ nguồn | 30 phút mỗi tình huống ưu tiên; tải tăng có kiểm soát | Có ngắt không, lỗi engine hay capture hay đường vô tuyến; không tăng buffer vô hạn |
| T29 | Khoảng cách/vật cản, Wi-Fi 2,4 so với 5 GHz, nhiều thiết bị Bluetooth, FxSound/Nahimic effects | 30 phút mỗi biến thể sau baseline; thay một biến mỗi lượt | Đường cong suy giảm và cấu hình khuyến nghị; không lấy kịch bản nhiễu để kết luận baseline đạt |
| T30 | So sánh SoundShare với Nahimic | M04, cùng tai nghe/nguồn/volume/điều kiện, 3 lượt 30 phút mỗi ứng dụng, đổi thứ tự chạy | So số lần và tổng thời gian gián đoạn, lệch âm học, chất lượng nghe và tài nguyên bằng cùng công cụ |

T30 chạy từng ứng dụng riêng, không chạy hai router chồng nhau. Các số liệu engine chỉ có ở SoundShare dùng để chẩn đoán, không dùng làm chỉ số duy nhất để kết luận tốt hơn Nahimic. Đầu ra acoustic hoặc đánh giá nghe có cùng phương pháp mới là cơ sở chung.

**10. Chỉ số nghiệm thu đề xuất**

Các con số dưới đây là mục tiêu kỹ thuật ban đầu, chưa phải số đo hoặc lời hứa sản phẩm. Sau P1 chỉ được điều chỉnh bằng ghi nhận lý do và báo rõ phần phạm vi thay đổi; không hạ tiêu chí để che lỗi.

| Chỉ số | Mục tiêu/tiêu chí | Cách xác minh |
| --- | --- | --- |
| Đúng hai nguồn | Cả A và B có mặt đồng thời ở topology được công bố | T11 và nội dung đo được; không chỉ trạng thái kết nối |
| Baseline engine | Sau 10 giây ổn định ban đầu, 0 underrun/overrun do engine trong lượt 60 phút | Log cùng số thứ tự frame, queue và sự kiện; khởi động/đổi thiết bị được ghi riêng |
| Nghe liên tục | 0 click/ngắt không chủ đích trong lượt baseline được công bố | Tín hiệu đo và nghe kiểm chứng; theo dõi cả ngắt ngắn, đặc biệt khoảng mất tín hiệu từ 20 ms trở lên |
| Chạy lâu | Không tăng gián đoạn theo thời gian, không queue trôi tới đầy/rỗng, không trôi lệch liên tục | Lượt 2 giờ, sau đó 8 giờ; lưu đồ thị theo thời gian |
| Đồng bộ tương đối | Mục tiêu khảo sát sau hiệu chỉnh: lệch tuyệt đối p95 ≤30 ms; độ lệch thay đổi thêm không quá 10 ms trong 2 giờ | Chỉ áp dụng khi có phép đo chung clock đáng tin cậy; không phải đồng bộ pha cho hệ loa cùng phòng |
| Độ trễ tổng | Đo và báo riêng, chọn buffer thấp nhất vẫn đạt độ mượt | Chưa đặt mức ms cố định cho mọi nguồn/loa; không bỏ qua độ khớp hình–tiếng |
| Khoanh vùng lỗi | Thiết bị lỗi không gây restart các nhánh đang khỏe | T17/T18, kiểm tra log và âm thanh đầu ra còn lại |
| Tài nguyên | Mục tiêu tổng UI + worker trung bình ≤5% tổng CPU và tổng working set ≤200 MB trên máy chuẩn ở M11 | Đo từng tiến trình và tổng sau warm-up; báo cấu hình resampler và CPU máy; chưa lấy làm bảo đảm mọi máy |
| Độ bền | Không crash/deadlock; số handle/thread không tăng sau chu kỳ mở/đóng; bộ nhớ đạt plateau | 100 chu kỳ start/stop; 20 chu kỳ thêm/bỏ; soak 8 giờ và công cụ native diagnostics |
| Cấu hình/thoát | Không có vòng lặp audio; route không bị bỏ quên khi kết thúc theo hướng dẫn | T14/T25/T26 |

Sự im lặng có trong bản nhạc không được đếm là lỗi; tiếng bị mất nhưng đã được codec che một phần cũng không thể suy ra số packet chính xác. Mọi lần re-prime do lỗi phải được ghi là gián đoạn, kể cả đã làm fade để bớt khó chịu.

**11. Hồ sơ kết quả và phân loại lỗi**

Mỗi lượt có mã run, build, topology, thiết bị, trạng thái G1–G6, cách thiết lập, fixture, thời gian, cấu hình buffer/offset, log, file đo nếu có, kết quả nghe và kết luận. Các trạng thái kết quả là Đạt, Không đạt, Bị chặn bởi điều kiện thử hoặc Chưa chạy; không dùng “đã hỗ trợ” cho Chưa chạy.

- **Chặn phát hành:** không nhận được nguồn bắt buộc, mất một nguồn trong chế độ hai nguồn được công bố, feedback, crash/deadlock, phát sai đích, lỗi cài/gỡ làm mất đường nghe bình thường.
- **Cao:** ngắt tiếng ở baseline, queue mất kiểm soát, một thiết bị lỗi làm hỏng cả nhóm, kết nối lại liên tục, hiệu chỉnh delay sai dấu.
- **Vừa:** phục hồi chậm có thông báo, profile phụ chưa tương thích, thông tin chẩn đoán thiếu nhưng chưa mất âm thanh.
- **Thấp:** vấn đề trình bày hoặc thao tác không ảnh hưởng đường âm thanh.

Không thử tiếp cùng một bài theo cách giống hệt sau ba lần thất bại mà không thu thêm bằng chứng. Chuyển sang topology nhỏ hơn và tách biến để tìm lỗi; đây là điều kiện thay đổi phương pháp test, không phải tự coi chức năng đã đạt.

**12. Điều kiện phát hành bản dùng thử**

Không còn lỗi chặn hoặc lỗi cao trên topology được công bố; có báo cáo chạy lâu, phục hồi và cài/gỡ; giới hạn hai nguồn/codec/thiết bị chưa đạt được ghi rõ. M11 chỉ được quảng bá khi chính M11 đạt, không suy từ M04 và M07 riêng lẻ.

Nếu hai nguồn cùng nhận được nhưng chỉ có bus trộn, phát hành với mô tả “nhận hỗn hợp hai nguồn” và không có thanh âm lượng riêng giả. Nếu hai nguồn không hoạt động đồng thời, phải báo đó là yêu cầu chưa đáp ứng trước khi chốt bản sản phẩm.

**Cơ sở cho phép đo**

WASAPI GetBuffer có cờ silent/discontinuity/timestamp error để xử lý capture và phát hiện một số gián đoạn; chúng không là phép đo mất packet vô tuyến. [Microsoft GetBuffer](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer).

Đo timing, buffer và tác vụ audio cần xét cả driver và hệ thống, không chỉ vòng lặp ứng dụng. [Microsoft Low Latency Audio](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio). Khả năng codec/profile có thể đổi khi microphone headset hoạt động. [Microsoft Bluetooth Classic Audio](https://learn.microsoft.com/en-us/windows-hardware/drivers/bluetooth/bluetooth-classic-audio).
