**SoundShare — kế hoạch triển khai và cơ chế hoạt động**

Phiên bản kế hoạch: 1.1, ngày 02/11/2026. Lựa chọn ngôn ngữ: C++20 cho toàn bộ ứng dụng, ưu tiên hiệu suất trên Windows theo yêu cầu người dùng. Đã triển khai core/backend và giao diện Win32 0.2.1; xem [báo cáo rà soát](docs/AUDIT_0.2.1.vi.md), [kiểm chứng giao diện](docs/DESKTOP_STATUS.vi.md) và [kiểm chứng backend](docs/BACKEND_STATUS.vi.md). Các mốc dưới đây vẫn là kế hoạch toàn sản phẩm, không có nghĩa tất cả đã hoàn thành.

Tài liệu đi kèm: [Danh sách chức năng](FEATURES.vi.md), [Kế hoạch kiểm thử](TEST_PLAN.vi.md), [Rà soát Windows và phần cứng](WINDOWS_FEASIBILITY.vi.md).

**1. Mục tiêu và thứ tự ưu tiên**

Xây dựng ứng dụng Windows để điện thoại và TV box coi PC như một loa Bluetooth. PC nhận âm thanh, sau đó phát tới các tai nghe Bluetooth và đầu ra dây/USB/HDMI do người dùng chọn. Có thêm chế độ chia sẻ âm thanh đang phát trên PC.

Ưu tiên theo thứ tự: tiếng liên tục và ổn định; không mất tiếng ở các đầu ra còn tốt khi một thiết bị lỗi; đồng bộ tương đối giữa đầu ra; độ trễ tổng thể phù hợp với việc sử dụng. Không ép độ trễ xuống thấp bằng mọi giá.

Cấu hình mục tiêu cần chứng minh là **điện thoại + TV box cùng phát vào PC, PC phát tới hai tai nghe Bluetooth**, có thể thêm đầu ra có dây. Đây là mục tiêu thử nghiệm, chưa phải khả năng đã được xác nhận. Không dùng phép cộng bitrate để thay cho kiểm thử.

Máy chuẩn đầu tiên: Windows 11 Pro 25H2 build 26200.9711, Realtek 8852CE, driver Bluetooth 2.10.1072.3003; Redmi Note 11 Pro và FPT Play Box+. Hai tai nghe cụ thể sẽ được ghi vào hồ sơ thử khi bắt đầu. Wi-Fi được ghi nhận ở 5 GHz trong lần kiểm kê; phải kiểm tra lại mỗi phiên thử.

**2. Những khả năng phải xác minh sớm**

| Mã | Câu hỏi cần trả lời | Bằng chứng bắt buộc | Hệ quả thiết kế |
| --- | --- | --- | --- |
| G1 | Điện thoại và TV box có xuất A2DP vào PC được không? | Từng nguồn phát tín hiệu riêng vào đầu ra dây và nghe/đo được | Chỉ thiết bị vượt thử nghiệm mới được ghi là tương thích |
| G2 | Hai nguồn có thực sự cùng truyền không? | Thu thấy nội dung phân biệt của cả hai nguồn đồng thời; thay đổi thứ tự kết nối/phát | Không lấy hai trạng thái Open thành bằng chứng hai luồng đang phát |
| G3 | Âm thanh receiver có đi qua đầu ra ảo và thu lại được không? | Đường receiver → bridge → capture hoạt động, không phát lặp | Quyết định cách tích hợp receiver vào engine |
| G4 | Thu được riêng từng nguồn hay chỉ hỗn hợp đã trộn? | Thử truy vết, tắt/mở từng nguồn; xác nhận có đường điều khiển ổn định, lặp lại được | Chỉ bật volume/mute/routing riêng khi có dữ liệu hoặc điều khiển thực sự riêng |
| G5 | Card chịu được nhận và phát đồng thời tới hai tai nghe không? | Bài chạy lâu của cấu hình mục tiêu, có âm thanh tại cả hai tai nghe | Chốt giới hạn cấu hình phát hành trên máy này |
| G6 | Định tuyến vào/ra có hoạt động khi đổi đầu ra Windows, ngủ/thức hoặc mất thiết bị không? | Nhật ký trạng thái và thử phục hồi | Chốt hành vi khôi phục và hướng dẫn cấu hình |

G4 có ba kết quả hợp lệ: A — hai nguồn âm thanh riêng; B — hai nguồn cùng nghe được nhưng đã trộn chung; C — chỉ một nguồn thực sự phát được. Ở B, sản phẩm vẫn có thể phát hỗn hợp tới mọi đầu ra nhưng không hứa chỉnh riêng từng nguồn. Ở C, phải ghi rõ giới hạn và báo lại để thống nhất phạm vi; không âm thầm coi yêu cầu hai nguồn là đã hoàn thành.

Nếu G3 không đạt, dừng mở rộng giao diện sản phẩm để điều tra đường capture được Windows hỗ trợ. Không giả định capture theo tiến trình sẽ thu riêng từng điện thoại vì Windows có thể phát qua thành phần hệ thống.

Không coi việc cắm thêm USB Bluetooth là phương án chia tải mặc định: tài liệu Microsoft nêu stack Bluetooth của Windows hỗ trợ một radio. Mọi giải pháp nhiều radio hoặc phần cứng ngoài phải là nhánh nghiên cứu riêng, chưa nằm trong kế hoạch cơ sở. [Microsoft Bluetooth FAQ](https://learn.microsoft.com/en-us/windows-hardware/drivers/bluetooth/bluetooth-faq).

**3. Kiến trúc lựa chọn**

| Thành phần | Công nghệ đề xuất | Trách nhiệm |
| --- | --- | --- |
| Giao diện desktop | C++20, Win32/Common Controls; Direct2D/DirectWrite cho meter hoặc đồ thị khi cần | Chọn thiết bị, điều khiển phiên, hiển thị tình trạng, tray |
| Tiến trình âm thanh | C++20, WASAPI, C++/WinRT | Sở hữu kết nối receiver, capture, xử lý và các luồng phát |
| Giao tiếp giao diện–engine | Named pipe nội bộ, có phiên bản giao thức | Lệnh và trạng thái; không chuyển khối PCM qua giao diện |
| Resampling | libsamplerate, bản được pin sau benchmark | Chuyển sample rate và bù chênh lệch tốc độ rất nhỏ |
| Đầu ra ảo cho bản thử | Bridge có sẵn, được ký và cài riêng | Đưa âm thanh Windows thành luồng ứng dụng thu được |
| Driver riêng | Nhánh sau, chỉ triển khai nếu cần sản phẩm độc lập | Endpoint mang tên SoundShare, transport PCM thực và đóng gói driver |
| Cấu hình/diagnostics | Tệp cấu hình có version, log sự kiện và số đo | Nhớ thiết bị, tham số hiệu chỉnh, phục vụ tìm lỗi |

C++20 được chọn cho cả UI và engine để chủ động quản lý bộ nhớ, luồng và các API Windows native. Dùng MSVC và Windows SDK được khóa phiên bản; không cần runtime .NET. Mục tiêu ban đầu là Windows 11 x64 trên máy hiện tại; chưa mở rộng ARM64/Windows 10 khi chưa có ma trận thử riêng. Đây là lựa chọn kiến trúc phù hợp mục tiêu CPU/RAM thấp và timing có thể kiểm soát, không phải kết quả benchmark chứng minh ngôn ngữ luôn nhanh nhất trong mọi tác vụ.

UI dùng control Windows chuẩn cho danh sách và thao tác; phần vẽ meter/đồ thị chỉ cập nhật khi cần, tách khỏi nhịp callback audio. Kiểm tra DPI, bàn phím và khả năng tiếp cận trong test giao diện. Direct2D là API đồ họa tăng tốc phần cứng của Windows, chỉ dùng ở nơi việc vẽ tùy chỉnh có ích. [Microsoft: các API multimedia native](https://learn.microsoft.com/en-us/windows/apps/develop/audio-video-camera).

Giao diện và engine chạy tách tiến trình. Đóng cửa sổ có thể thu về tray trong khi phiên tiếp tục. Lỗi giao diện không trực tiếp giải phóng receiver, vì các đối tượng receiver được giữ ở engine. Engine lỗi vẫn gây gián đoạn; cơ chế khởi động lại không được gọi là phục hồi không ngắt tiếng.

libsamplerate hỗ trợ trạng thái streaming và thay đổi tỷ lệ chuyển mẫu êm qua tham số xử lý; không gọi hàm buộc nhảy tỷ lệ để sửa drift bình thường. [Tài liệu streaming](https://libsndfile.github.io/libsamplerate/api_full.html). Thư viện hiện dùng BSD 2-Clause; giữ thông báo cần thiết khi phân phối. [Giấy phép](https://github.com/libsndfile/libsamplerate/blob/master/COPYING). Phiên bản/commit và cấu hình chất lượng sẽ được khóa sau bài đo CPU và chất lượng ở P2.

**4. Đường đi của âm thanh**

```text
Điện thoại ── A2DP ─┐
                    ├─ Windows Bluetooth receiver ── đầu ra ảo ── capture ─┐
TV box ───── A2DP ──┘                                                     │
                                                                          ├─ bus âm thanh chung
Âm thanh PC ───────── đầu ra ảo đã chọn ────────────────────────────────────┘        │
                                                                                   ├─ nhánh A → tai nghe Bluetooth A
                                                                                   ├─ nhánh B → tai nghe Bluetooth B
                                                                                   └─ nhánh C → dây / USB / HDMI
```

Hai đường dùng cùng một đầu ra ảo chỉ được capture **một lần**. Sơ đồ không hàm ý receiver đã cung cấp hai luồng PCM riêng. Khi Windows đã trộn điện thoại, TV box và âm thanh PC vào cùng endpoint, engine nhận một hỗn hợp; không thể tách chính xác các nguồn sau đó bằng một thanh chỉnh âm lượng.

Nếu G4 chứng minh có đường riêng, mỗi nguồn có capture/queue/resampler đầu vào riêng rồi mới trộn. Nếu không, UI quản lý kết nối nguồn riêng nhưng chỉ điều chỉnh âm thanh của hỗn hợp. Độ lệch giữa các bản nhạc khác nhau từ hai nguồn không có mục tiêu đồng bộ nội dung; chỉ các đầu ra của cùng hỗn hợp cuối phải được căn với nhau.

Windows dùng thành phần Bluetooth của hệ điều hành để nhận và phát media cục bộ; AudioPlaybackConnection điều khiển kết nối, không phải bộ đọc packet hoặc API PCM riêng theo nguồn. [Hướng dẫn receiver](https://learn.microsoft.com/en-us/windows/apps/develop/media-playback/enable-remote-audio-playback), [API receiver](https://learn.microsoft.com/en-us/uwp/api/windows.media.audio.audioplaybackconnection).

**5. Khởi động, định tuyến và kết thúc phiên**

1. Kiểm kê nguồn A2DP bằng DeviceWatcher và các endpoint Windows; lưu ID ổn định, tên hiển thị, trạng thái và vai trò. Không dùng tên thân thiện làm khóa duy nhất.
2. Kiểm tra bridge, đầu ra vật lý được chọn và đầu ra mặc định theo các vai trò Windows. Endpoint phát của bridge không được chọn làm đích fan-out, tránh vòng lặp.
3. Với PoC, người dùng chọn đầu ra ảo trong Sound Settings theo hướng dẫn. Ứng dụng đọc và xác minh trạng thái; không coi API đọc default endpoint là API đặt default. Không đặt một COM interface không được tài liệu hóa thành phụ thuộc bắt buộc của sản phẩm.
4. Engine mở các đầu ra vật lý ở shared mode, kiểm tra format và kích thước buffer được hỗ trợ; sau đó mở capture tương ứng. Đầu ra chưa có dữ liệu phát số 0, không đọc vùng nhớ chưa khởi tạo.
5. Cho phép/mở receiver của nguồn đã chọn; giao diện hiển thị trạng thái kết nối thực. Chỉ nhận định nguồn đang phát nếu có tín hiệu kiểm chứng được, không suy từ trạng thái Open.
6. Thu tín hiệu, chuyển sang format nội bộ, tích lũy đủ dự trữ rồi phát có ramp âm lượng ngắn để tránh click khởi động.
7. Thêm đầu ra khi đang phát: mở và nạp đệm riêng nhánh mới, căn theo timeline hiện tại, rồi tăng âm lượng. Không khởi động lại các nhánh đang tốt.
8. Bỏ một đầu ra: giảm âm lượng nhánh đó trước khi đóng. Thiếu một endpoint không làm hủy toàn bộ danh sách lựa chọn.
9. Chế độ “Chỉ loa chính” giữ đường âm thanh qua một đầu ra dự phòng đã chọn. “Dừng nhận” đóng receiver tương ứng. Phân biệt rõ với “Thoát ứng dụng”.
10. Khi thoát/gỡ bản PoC, hướng dẫn trở về đầu ra vật lý và xác minh không còn default trỏ vào bridge mà không có ứng dụng chuyển tiếp. Chưa cam kết tự khôi phục default bằng API công khai khi chưa xác minh được đường thực hiện.

Đầu ra ảo có thể là VB-CABLE trong thử nghiệm: endpoint Playback nhận âm thanh, endpoint Recording tương ứng đưa dữ liệu sang ứng dụng thu. Nếu thu endpoint Recording thì không đồng thời thêm loopback Playback của cùng bridge. Tên “Input/Output” của bridge có thể gây nhầm, nên lưu ánh xạ rõ trong cấu hình. [Tài liệu VB-CABLE](https://vb-audio.com/Cable/).

Chưa cài, tải hay đóng gói kèm driver trong giai đoạn lập kế hoạch. Khi thử thực tế phải xác định giấy phép, nguồn gói, yêu cầu cài đặt và khởi động lại; không mặc định được tái phân phối driver bên thứ ba trong installer SoundShare.

**6. Cơ chế xử lý ưu tiên độ mượt**

| Lớp | Cách làm | Giới hạn cần giữ |
| --- | --- | --- |
| Chuẩn hóa dữ liệu | PCM float32, stereo, 48 kHz làm bus ban đầu; chuyển đổi format thực của endpoint | Không buộc mọi phần cứng tự hỗ trợ đúng 48 kHz; 44,1/48/96 kHz được thử qua adapter |
| Capture | Đọc hết packet sẵn có, giữ timestamp, xử lý SILENT thành zero và đánh dấu discontinuity | Không coi silence là mất kết nối; không gọi các cờ này là số gói Bluetooth bị mất |
| Fan-out | Bus chung có số thứ tự; từng nhánh có con trỏ đọc/bộ đệm riêng | Nhánh chậm không được giữ khóa hoặc chặn producer và các nhánh khác |
| Luồng thời gian thực | Event-driven khi driver hỗ trợ, đăng ký tác vụ audio, vùng nhớ cấp trước | Không ghi log ra đĩa, chạy IPC đồng bộ, gọi UI, cấp phát thường xuyên hoặc giữ khóa lâu trong callback |
| Dự trữ | Benchmark các mức đệm ứng dụng 40/80/120/240 ms; chọn mức thấp nhất vượt bài mượt | Đây là điểm thử, chưa phải default; không nhầm với buffer phần cứng WASAPI hoặc độ trễ toàn đường |
| Drift | Kết hợp vị trí phát/QPC và mức đầy queue; lọc nhiễu, đổi ratio nhẹ, có clamp và giới hạn tốc độ đổi | Không sửa bình thường bằng cách cắt/lặp nguyên khối âm thanh; bỏ số đo timestamp lỗi |
| Chỉnh volume | Tăng/giảm có ramp; ưu tiên gain riêng của luồng SoundShare | Không tự thay volume toàn thiết bị khiến ứng dụng khác bị ảnh hưởng |
| Trộn | Nếu có nguồn riêng thì gain staging trước khi cộng; bảo vệ sample peak ở bus chung | Không mặc định cộng hai nguồn full-scale; mute một nguồn phải làm êm |
| Bù delay | Offset riêng từng đầu ra, khởi đầu chỉnh thủ công, lưu theo thiết bị và cấu hình format | Chỉ bù được chậm thêm; delay phần mềm không làm loa Bluetooth phát sớm hơn |
| Thiết bị lỗi | Tháo nhánh khỏi timeline hoạt động, giữ các nhánh còn tốt, kết nối lại có backoff | Không để một thiết bị lỗi kéo tăng đệm hoặc retry vô hạn cho cả nhóm |

Mức đệm, giới hạn ratio và khoảng ramp là tham số cần đo. Đề xuất ban đầu: dung lượng queue hữu hạn khoảng 2 giây mỗi nhánh; offset thủ công 0–1000 ms; thử clamp drift khoảng ±300 ppm rồi thử lỗi giả lập rộng hơn. Phải dành đủ dung lượng cho cả dự trữ, offset và lịch sử resampler; nếu cấu hình vượt dung lượng thì từ chối hoặc cấp lại trước khi chạy, không ghi tràn.

Điều chỉnh drift là thay đổi tỷ lệ rất nhỏ để giữ mức đệm ổn định. Khi cần tăng dự trữ nhiều sau một gián đoạn, không nhảy con trỏ giữa bản nhạc và gọi đó là sửa êm. Nhánh lỗi phải có chu kỳ giảm âm → nạp lại → căn lại → tăng âm, và lần gián đoạn đó vẫn được ghi vào thống kê.

Khi đang phát bình thường, không liên tục đổi target buffer chỉ vì một số đo nhiễu. Chọn buffer lúc bắt đầu; đánh giá lại ở điểm im lặng phù hợp hoặc khi thật sự phục hồi lỗi. Buffer không được tăng mãi để che một liên kết vô tuyến kém.

IAudioClock hỗ trợ theo dõi vị trí phát theo thời gian hệ thống; giá trị đó không thay cho phép đo tiếng thực sự đến tai nghe. [Tài liệu IAudioClock](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getposition).

**7. Trạng thái, phục hồi và quan sát lỗi**

| Đối tượng | Trạng thái chính | Hành vi |
| --- | --- | --- |
| Phiên tổng | Chưa cấu hình → Sẵn sàng → Nạp đệm → Chia sẻ → Đang khôi phục / Chỉ loa chính → Kết thúc | Lệnh start/stop lặp không tạo thêm engine hoặc thêm capture cùng endpoint |
| Kết nối nguồn | Phát hiện → Cho phép nhận → Đang mở → Đã mở → Đang đóng / Thất bại | Tách trạng thái mong muốn khỏi trạng thái Windows trả về |
| Nhánh đầu ra | Chờ thiết bị → Đang mở → Nạp đệm → Đang phát → Đang khôi phục / Đã bỏ chọn | Mỗi nhánh phục hồi độc lập |

Chỉ điều khiển kết nối lại trong phiên do người dùng bật. Có thể nhớ lựa chọn nhưng không tự giành âm thanh của điện thoại sau mỗi lần khởi động. Nguồn đang pause không bị đóng rồi mở lại theo timer.

Device invalidated, format thay đổi hoặc HFP được bật phải tạo sự kiện cấu hình lại; xóa hiệu chỉnh không còn đúng hoặc đánh dấu cần đo lại. Khi máy ngủ, tạm dừng có chủ đích; khi thức, kiểm kê lại rồi mở các thiết bị còn khả dụng.

Log ghi thời gian đơn điệu, topology, trạng thái, lỗi API, số lần thiếu/tràn đệm, discontinuity, tỷ lệ resample, mức đầy queue và số lần khôi phục. Writer chạy ngoài đường realtime, có giới hạn dung lượng. Không ghi nội dung âm thanh thật mặc định. Đo acoustic hoặc lưu audio chỉ dùng fixture kiểm thử được bật riêng.

**8. Cấu trúc mã nguồn dự kiến**

```text
apps/SoundShare.Desktop/         C++ Win32, màn hình chính, tray, model trạng thái UI
native/SoundShare.Worker/        tiến trình engine, receiver, IPC server
native/audio-core/              timeline, ring buffer, conversion, mix, drift, delay
native/windows-audio/           WASAPI capture/render, clocks, notifications
native/bluetooth-receiver/      C++/WinRT, DeviceWatcher, connection lifecycle
shared/protocol/                đặc tả lệnh/sự kiện và version
tests/core/                     mô phỏng clocks, queues, resampler, state transitions
tests/windows-integration/      harness tương tác với endpoint và thiết bị thật
tests/fixtures/                 tín hiệu tổng hợp và mô tả cách phát
tools/diagnostics/              thu số đo, tổng hợp kết quả và so sánh các phiên
packaging/                      gói ứng dụng, dependency và thông báo bên thứ ba
docs/                           thiết kế, thao tác vận hành, hồ sơ tương thích
```

Đây là cấu trúc sẽ tạo khi bắt đầu code, chưa phải các tệp đã tồn tại. Giao thức IPC cần lệnh inventory, enable/open/close receiver, configure route, start/stop, set output gain/mute/delay, get status và export diagnostics. Lệnh có ID để đối chiếu kết quả, lỗi có mã ổn định; telemetry được giảm tần suất trước khi đưa lên UI. Pipe chỉ cho phép tài khoản/phiên cục bộ được dùng ứng dụng.

**Quy trình build và quản lý thay đổi**

Khóa phiên bản Windows SDK, MSVC, CMake và dependency native khi bắt đầu P1/P2; dùng cấu hình build x64 thống nhất cho UI và worker. Thiết lập CMake cho toàn bộ ứng dụng và lockfile dependency; không phụ thuộc một máy phát triển đã cài sẵn ngẫu nhiên các DLL.

CI trên Windows thực hiện build Release, kiểm thử lõi mô phỏng, kiểm tra version giao thức và tạo artifact. Kiểm thử Bluetooth thật là lượt riêng trên máy có thiết bị, không được thay bằng CI dùng endpoint giả. Mỗi lỗi xử lý âm thanh đã tìm được nên có fixture hoặc bài tái hiện phù hợp trước khi sửa.

Các mốc code tạo thay đổi nhỏ có thể kiểm tra: inventory → một nguồn/một đầu ra → nhiều đầu ra → bridge → hai nguồn → drift/phục hồi → UI đầy đủ. Mỗi mốc có log và quyết định kỹ thuật đi kèm. Không chuyển cùng lúc driver, thuật toán đệm và thiết bị thử vì sẽ khó xác định nguyên nhân thay đổi kết quả.

UI, worker và các thư viện native được phát hành cùng phiên bản; kiểm tra các dependency MSVC cần đóng gói theo cấu hình link đã chọn trên máy sạch. Kiểm tra khởi động khi thiếu/sai phiên bản worker hoặc thư viện phải có lỗi rõ, không làm ứng dụng treo hoặc mất tiếng không giải thích được. Symbol debug được lưu riêng theo build để phân tích crash.

P1 phải kiểm tra khởi tạo COM/WinRT và vòng đời receiver trong tiến trình desktop, cùng yêu cầu package identity/capability nếu API thực tế đòi hỏi. Không chốt kiểu installer trước khi kiểm chứng điểm này. P8 cần pipeline driver riêng; không đưa yêu cầu ký và cài driver vào build ứng dụng thông thường một cách ngầm định.

**9. Kế hoạch code theo mốc và điều kiện hoàn thành**

| Mốc | Công việc code cụ thể | Đầu ra để kiểm tra | Điều kiện chuyển mốc |
| --- | --- | --- | --- |
| P0 — thiết kế | Chốt phạm vi, feature IDs, topology mục tiêu, chỉ số đo, quyết định driver PoC | Ba tài liệu kế hoạch hiện tại | Các giả định và phần phụ thuộc phần cứng được đánh dấu |
| P1 — kiểm chứng Windows | Viết harness inventory và receiver nhỏ; mở từng nguồn/hai nguồn; capture bridge; phát tín hiệu tới 1 rồi 2 đầu ra | Capability report, topology thực, log và tín hiệu thử; chưa cần UI sản phẩm | Trả lời G1–G4, thử sơ bộ G5; có quyết định rõ nếu đường hai nguồn không đạt |
| P2 — lõi xử lý | Ring buffer, timeline, format conversion, resampler streaming, gain/ramp, mix và meter; giả lập clocks | Native library và bài mô phỏng chạy không cần Bluetooth | Test mất/thiếu dữ liệu, wrap, drift hai chiều và tràn được xử lý có giới hạn |
| P3 — WASAPI thực | Enumerate/open/capture/render, callback, notification, clock, telemetry; file thử → nhiều đầu ra | Worker phát ổn định tới dây/USB rồi Bluetooth | Bài T01–T08 và baseline từng topology đạt |
| P4 — ghép receiver | Đưa kết nối receiver vào worker; tích hợp bridge; ngăn feedback; quản lý nguồn và hỗn hợp | Điện thoại/TV box → PC → đầu ra; hai nguồn theo kết quả G4 | Bài T09–T15, không trùng capture và không có chức năng tách nguồn giả |
| P5 — đồng bộ/phục hồi | Điều khiển drift có giới hạn, offset, join/leave, re-prime, device invalidated, ngủ/thức, worker restart | Phiên chạy lâu và lỗi thiết bị được khoanh vùng | T16–T22; không làm dừng nhánh còn tốt vì một nhánh lỗi |
| P6 — giao diện sử dụng | Màn hình nguồn/đầu ra, chỉ báo, volume/mute, hướng dẫn route, profile, tray, diagnostics | Luồng sử dụng từ mở app tới kết thúc phiên | T23–T25; UI chỉ hiển thị các khả năng thực sự đã xác minh |
| P7 — ổn định/phát hành | A/B với Nahimic, stress/soak, ngân sách tài nguyên, gói x64, kiểm tra máy sạch và uninstall | Bản thử dùng cá nhân + hồ sơ thiết bị/config đã đạt | T26–T30, không còn lỗi chặn; ghi rõ các topology chưa hỗ trợ |
| P8 — driver riêng, tùy chọn | Thiết kế endpoint và PCM transport thật; phát triển driver; ký, kiểm thử và cài/gỡ | Gói không phụ thuộc bridge cài riêng | Chỉ bắt đầu khi đường ứng dụng đã chứng minh giá trị và có yêu cầu phân phối độc lập |

Giao diện tối thiểu/harness phục vụ P1 không phải toàn bộ giao diện P6. Có thể chuẩn bị lõi P2 trong lúc chờ thiết bị, nhưng không xây chức năng sản phẩm dựa trên giả định G2/G3 chưa đạt. Không ấn định ngày kết thúc trước P1: phạm vi thay đổi đáng kể nếu Windows không đưa được luồng nhận tới bridge hoặc không nhận đồng thời hai nguồn.

**10. Hạng mục driver riêng nếu triển khai P8**

Driver cần có endpoint phát ảo và đường đọc PCM thực cho worker; format negotiation, vị trí/clock, queue hữu hạn, xử lý worker biến mất, restart và nhiều phiên mở. Worker chỉ đọc bus đầu vào của driver; không tự loop output vật lý về input.

SYSVAD là điểm tham khảo về driver Windows, không cung cấp sẵn đầy đủ virtual cable chuyển dữ liệu mong muốn. P8 còn có build tái lập, ký driver theo yêu cầu hiện hành của Microsoft, kiểm tra Secure Boot/HVCI, HLK phù hợp, nâng cấp/cài/gỡ và phục hồi route. Không yêu cầu người dùng cuối bật test-signing để chạy bản phát hành. [Microsoft SYSVAD](https://learn.microsoft.com/en-us/samples/microsoft/windows-driver-samples/sysvad-virtual-audio-device-driver-sample/).

**11. Định nghĩa hoàn thành**

Một topology được công bố hỗ trợ khi nhận được nội dung đúng, tất cả đầu ra được chọn thực sự phát, vượt bài đo ổn định và phục hồi tương ứng, và có hồ sơ OS/driver/thiết bị/codec nếu đọc được. Ghép đôi thành công hoặc mở đủ handle không đủ để đánh dấu hoàn thành.

Ứng dụng không được báo “không mất gói Bluetooth” từ số đếm queue PCM. Tốt hơn Nahimic chỉ được kết luận từ số đo đầu ra chung trong phép A/B công bằng. Tính năng hai nguồn không được đánh dấu đạt nếu thực tế chỉ một nguồn phát hoặc nguồn thứ hai làm ngắt nguồn thứ nhất.
