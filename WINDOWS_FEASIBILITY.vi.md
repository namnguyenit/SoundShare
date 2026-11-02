**Rà soát khả năng triển khai SoundShare trên Windows**

Ngày rà soát: 02/11/2026.

Báo cáo này ghi nhận giai đoạn rà soát ban đầu. Sau đó đã triển khai core/backend; [kết quả kiểm chứng hiện tại](docs/BACKEND_STATUS.vi.md) được ghi riêng để không nhầm với các giả định dưới đây.

**Kết luận**

Có thể xây dựng ứng dụng Windows cho cả hai hướng: phát một nguồn ra nhiều đầu ra âm thanh và biến PC thành thiết bị nhận âm thanh Bluetooth. Theo yêu cầu đã làm rõ, ưu tiên là nghe liên tục, mượt, hạn chế giật/ngắt; đồng bộ tốt nhất có thể, không yêu cầu tuyệt đối. Khả năng phát ổn định vẫn cần xác nhận theo tổ hợp radio, driver và thiết bị đã thử nghiệm.

Khuyến nghị triển khai một ứng dụng Windows riêng, dùng NextSound làm tham khảo về trải nghiệm nhận Bluetooth. Cần xác minh đường nhận Bluetooth → thu âm thanh → phát nhiều đầu ra trước khi xây dựng toàn bộ sản phẩm.

**Phạm vi đã kiểm tra**

- Thư mục `D:\soundshare` chưa có mã nguồn ứng dụng tại thời điểm bắt đầu rà soát.
- Đã đọc README, tài liệu kiến trúc và các tệp backend, audio, giao diện, kiểm thử của NextSound tại commit `8fb635bd21728ebd4e8c6bbc8e71b94c6f0c8934`.
- Đã đối chiếu tài liệu Microsoft về WASAPI, loopback, đồng hồ âm thanh, Bluetooth receiver và Shared Audio; đối chiếu tài liệu Android về thu âm thanh ứng dụng.
- Đã kiểm tra thông tin Windows và tên thiết bị bằng thao tác chỉ đọc. Chưa thực hiện thử nghiệm phát/thu, chưa đo độ trễ và chưa thay đổi cấu hình âm thanh hoặc cài driver.

**Khả năng theo yêu cầu**

| Yêu cầu | Đánh giá | Điều kiện chính |
| --- | --- | --- |
| Chọn nhiều đầu ra dây, USB, HDMI | Khả thi cao | Windows phải cung cấp từng đầu ra có thể mở để phát |
| Phát cùng nội dung ra nhiều loa Bluetooth | Khả thi có điều kiện | Radio, driver và từng loa phải cho phép các luồng phát đồng thời |
| Trộn đầu ra Bluetooth với đầu ra dây | Khả thi có điều kiện | Phải bù trễ và sửa lệch tốc độ phát giữa thiết bị |
| Đồng bộ tốt nhất có thể, ưu tiên nghe liên tục | Phù hợp làm mục tiêu sản phẩm | Cần bộ đệm, bù lệch và kiểm thử từng tổ hợp thiết bị |
| Một điện thoại phát Bluetooth vào PC | Có API Windows chính thức | Nguồn hỗ trợ A2DP Source; ghép đôi và mở kết nối thành công |
| TV box phát Bluetooth vào PC | Phụ thuộc model và firmware | TV box phải có chức năng xuất media sang loa Bluetooth |
| Quản lý nhiều nguồn và chọn nguồn đang nghe | Khả thi về thiết kế | Cần thử đóng/mở, chuyển nguồn và kết nối lại |
| Nhiều nguồn Bluetooth cùng phát vào PC | Chưa xác nhận | Cần thử trên radio, driver và thiết bị thực tế |
| Thu riêng, chỉnh âm lượng riêng từng nguồn Bluetooth | Chưa xác nhận đường triển khai | API receiver không trực tiếp cung cấp các luồng PCM riêng |
| Nhiều nguồn gửi qua LAN bằng ứng dụng riêng | Khả thi về kiến trúc | Cần phần mềm gửi, bộ đệm mạng và quyền thu âm thanh ở thiết bị nguồn |

“Bất kỳ loa gì” nên được định nghĩa là đầu ra âm thanh mà Windows nhận diện và ứng dụng mở được. Loa mạng không xuất hiện như đầu ra Windows cần thêm giao thức hoặc phần mềm nhận riêng; loa analog trên cùng một cổng chia không trở thành các thiết bị độc lập.

**NextSound có thể tái sử dụng đến đâu?**

NextSound hiện là ứng dụng Linux: Python/GTK, BlueZ qua D-Bus, PipeWire/WirePlumber và `pactl`. Các thành phần này thực hiện kết nối, giải mã và định tuyến; ứng dụng không tự xây dựng toàn bộ bộ máy xử lý âm thanh. Backend Windows cần được viết lại. [Kiến trúc NextSound](https://github.com/namnguyenit/NextSound/blob/8fb635bd21728ebd4e8c6bbc8e71b94c6f0c8934/docs/architecture.md).

Trong mã đã kiểm tra, `AudioBackend.set_default()` chọn một đầu ra mặc định rồi chuyển các luồng điện thoại sang đầu ra đó. Giao diện cũng dùng một lựa chọn đầu ra. Vì vậy repo này chưa cung cấp bộ phát nhiều loa có cơ chế đồng bộ riêng để lấy dùng ngay. [audio.py](https://github.com/namnguyenit/NextSound/blob/8fb635bd21728ebd4e8c6bbc8e71b94c6f0c8934/src/nextsound/audio.py), [window.py](https://github.com/namnguyenit/NextSound/blob/8fb635bd21728ebd4e8c6bbc8e71b94c6f0c8934/src/nextsound/window.py).

Nên tham khảo cách hiển thị nguồn, trạng thái kết nối, mở/dừng nhận và chọn đầu ra. Không chuyển nguyên khả năng SBC/AAC/LDAC của backend Linux thành cam kết trên Windows. Quyền dùng mã ứng dụng được nêu trong [LICENSE của NextSound](https://github.com/namnguyenit/NextSound/blob/8fb635bd21728ebd4e8c6bbc8e71b94c6f0c8934/LICENSE); các thành phần bên thứ ba cần được xét riêng nếu tái phân phối.

**Tính năng phát ra nhiều thiết bị**

WASAPI cho phép ứng dụng làm việc với từng đầu ra âm thanh. Hướng triển khai là mở một luồng phát riêng cho mỗi đầu ra được chọn, cấp cùng nội dung âm thanh cho các luồng đó. Đây là cơ sở kỹ thuật khả thi, chưa phải bảo đảm mọi tổ hợp phần cứng đều hoạt động đồng thời. [Microsoft: About WASAPI](https://learn.microsoft.com/en-us/windows/win32/coreaudio/wasapi).

Thiết kế đồng bộ cần giải quyết hai vấn đề độc lập:

1. **Chênh lệch độ trễ ban đầu:** giữ âm thanh ở đầu ra nhanh hơn để chờ đầu ra chậm hơn. Ví dụ minh họa, nếu đo được hai loa trễ 25 ms và 180 ms, nhánh nhanh cần thêm khoảng 155 ms. Đây là ví dụ tính toán, không phải số đo trên máy hiện tại.
2. **Lệch dần theo thời gian:** các thiết bị có thể tiêu thụ mẫu âm thanh với tốc độ hơi khác nhau. Cần theo dõi vị trí phát và lượng dữ liệu đệm để điều chỉnh tốc độ lấy mẫu rất nhỏ, liên tục, thay vì chỉ đặt một mức delay cố định.

Microsoft cung cấp vị trí phát gắn với mốc thời gian hệ thống qua `IAudioClock`. Từ đó có thể thiết kế bộ điều khiển bù lệch; độ chính xác thực tế vẫn phụ thuộc driver và thiết bị. [Microsoft: IAudioClock.GetPosition](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getposition).

Không nên coi thông số độ trễ do Windows báo là phép đo chắc chắn thời điểm âm thanh thoát ra khỏi mọi loa Bluetooth. `GetStreamLatency` trả về một giá trị tối đa của luồng, cố định trong vòng đời đối tượng; cần đo thực tế để kiểm chứng toàn bộ đường phát. [Microsoft: GetStreamLatency](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-getstreamlatency).

Đề xuất có hiệu chỉnh delay thủ công ở bản đầu, lưu theo thiết bị, rồi bổ sung đo tự động bằng microphone. Đo âm học cần xét vị trí loa/microphone và tiếng vọng phòng; việc đo lại sau khi đổi chế độ loa hoặc kết nối lại có thể cần thiết. Đây là đề xuất thiết kế, chưa được thử nghiệm.

**Vì sao cần đầu ra âm thanh ảo?**

WASAPI loopback thu âm thanh đang được phát ở một đầu ra. Nó hữu ích để thử chức năng nhân bản, nhưng nếu đầu ra đó là loa vật lý, loa đã phát trước khi bản thu được chuyển tiếp. Không thể dùng bộ đệm phía sau để trì hoãn ngược lại âm thanh đã phát. [Microsoft: Loopback Recording](https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording).

Để chủ động trì hoãn tất cả loa, kiến trúc đề xuất là:

```text
Âm thanh ứng dụng trên PC ────────────────┐
                                       ▼
Nguồn Bluetooth → Windows receiver → Đầu ra ảo → Thu âm thanh
                                                   │
Nguồn LAN có ứng dụng gửi ──────────────────────────┤
                                                   ▼
                                     Chọn nguồn / trộn âm thanh
                                                   │
                                    Mốc thời gian và dữ liệu chung
                                                   │
                         ┌─────────────────────────┼────────────────────┐
                         ▼                         ▼                    ▼
                  Đệm + bù trễ A             Đệm + bù trễ B      Đệm + bù trễ C
                         ▼                         ▼                    ▼
                   WASAPI loa dây            WASAPI USB DAC     WASAPI Bluetooth
```

Nhánh Bluetooth → đầu ra ảo trong sơ đồ là giả thuyết cần xác minh: phải kiểm tra Windows thực sự đưa luồng nhận tới đầu ra được chọn và ứng dụng thu lại được. Không được quảng bá sơ đồ này như một đường truyền đã hoạt động.

Bản thử nghiệm có thể dùng thiết bị âm thanh ảo có sẵn. Nếu sản phẩm cần đầu ra ảo mang tên SoundShare, sẽ có thêm hạng mục phát triển, ký và phân phối driver. SYSVAD của Microsoft là mẫu tham khảo, không phải dây âm thanh ảo hoàn chỉnh có thể đóng gói dùng ngay. [Microsoft: SYSVAD](https://learn.microsoft.com/en-us/samples/microsoft/windows-driver-samples/sysvad-virtual-audio-device-driver-sample/).

Phải ngăn thu lại chính các nhánh phát của SoundShare, nếu không có thể tạo vòng lặp âm thanh. Cũng cần thử ứng dụng dùng chế độ độc quyền và nội dung được bảo vệ; loopback không bảo đảm thu mọi nội dung.

**Tính năng nhận điện thoại/TV box**

Windows có `AudioPlaybackConnection` từ Windows 10 version 2004, build 19041. API cho phép cho phép nhận, mở và đóng kết nối âm thanh từ thiết bị từ xa. API công khai không có bộ đọc PCM, bộ chọn codec hay tham số đầu ra riêng cho từng nguồn. Đây là giới hạn cần xét khi ghép receiver với bộ xử lý của SoundShare. [Microsoft: AudioPlaybackConnection](https://learn.microsoft.com/en-us/uwp/api/windows.media.audio.audioplaybackconnection).

Tài liệu mẫu của Microsoft quản lý các kết nối bằng danh sách theo thiết bị. Điều này hỗ trợ thiết kế giao diện nhiều nguồn, nhưng chưa đủ để kết luận nhiều nguồn cùng truyền thành công hoặc có thể tách âm thanh từng nguồn. Cần thử riêng từng trường hợp. [Microsoft: Enable remote audio playback](https://learn.microsoft.com/en-us/windows/apps/develop/media-playback/enable-remote-audio-playback).

Phạm vi ban đầu nên là nhiều nguồn được ghi nhớ, người dùng chọn một nguồn đang nghe. Chức năng nhiều nguồn cùng phát và trộn âm lượng riêng nên là hạng mục thử nghiệm tiếp theo.

Đối với TV box, cần kiểm tra chức năng ghép với loa Bluetooth và xuất âm thanh media. Có Bluetooth cho remote hoặc bàn phím không tự chứng minh có A2DP Source. Việc Windows đã ghi nhận tên FPT Play Box+ cũng chưa xác nhận đường âm thanh này hoạt động.

Khi dùng Bluetooth cả chiều vào và chiều ra, tổng độ trễ có thêm cả hai chặng. Bù để các loa phát cùng nhau không tự làm hình ảnh trên điện thoại/TV box khớp tiếng. Nếu cần xem phim khớp môi, phải đo và kiểm tra khả năng chỉnh ở thiết bị phát hoặc trình phát nguồn.

Về codec, tài liệu Windows về A2DP phát ra thiết bị nêu SBC, AAC và các biến thể aptX tùy hệ thống. Không dùng bảng codec chiều phát để suy ra codec nhận vào, và không cam kết nhận LDAC chỉ vì NextSound trên Linux có tính năng đó. Mở microphone tai nghe Bluetooth Classic còn có thể làm hệ thống chuyển sang HFP và thay đổi chất lượng phát. [Microsoft: Bluetooth Classic Audio](https://learn.microsoft.com/en-us/windows-hardware/drivers/bluetooth/bluetooth-classic-audio).

**Phương án nhiều nguồn qua mạng LAN**

Nếu mục tiêu dài hạn là nhiều nguồn đồng thời, có thể bổ sung phần mềm gửi trên điện thoại/TV box. Mỗi nguồn gửi một luồng riêng, cho phép PC quản lý âm lượng và định tuyến trước khi trộn. Đây là lựa chọn kiến trúc cần xây thêm ứng dụng nguồn, giao thức truyền, xác thực thiết bị và bộ đệm chống dao động mạng.

Không thể cam kết phần mềm gửi lấy được mọi âm thanh trên thiết bị. Android 10 có AudioPlaybackCapture, nhưng cần quyền và sự đồng ý của người dùng; ứng dụng đang phát có thể không cho thu. Phải thử đúng firmware TV box và các ứng dụng media mục tiêu. [Android: Capture audio playback](https://developer.android.com/media/platform/av-capture).

**Shared Audio có sẵn trên Windows giải quyết được gì?**

Microsoft đã ghi nhận Shared Audio trong bản cập nhật tháng 5/2026 cho nhánh build 26200/26100: truyền cùng âm thanh tới hai thiết bị tương thích qua Bluetooth LE Audio broadcast. Có thể cân nhắc dùng tính năng hệ thống cho tổ hợp thiết bị phù hợp; nó không thay thế yêu cầu trộn nhiều loa dây, USB và Bluetooth Classic tùy ý. [Microsoft: KB5089573](https://support.microsoft.com/en-au/servicing/os/windows-11/2026/05/may-26-2026-kb5089573-os-builds-26200-8524-and-26100-8524-preview).

Chưa xác minh PC này có phần cứng, driver và phụ kiện đáp ứng Shared Audio. Không suy ra hỗ trợ LE Audio hoặc broadcast từ tên adapter hay số phiên bản Bluetooth.

**Thông tin máy hiện tại**

- Windows 11 Pro 25H2, build **26200.9711**.
- Adapter được Windows ghi nhận: **Realtek Bluetooth Adapter**, xác định là **8852CE** khi đối chiếu mã `USB\VID_0BDA&PID_5852` với mục tương ứng trong driver `C:\Windows\INF\oem95.inf`.
- Driver Bluetooth đang dùng: **2.10.1072.3003**, ngày 28/09/2024. Card Wi-Fi được Windows ghi nhận là **Realtek 8852CE WiFi 6E PCI-E NIC**.
- Khi kiểm tra bổ sung, Wi-Fi đang hoạt động ở **5 GHz**, kênh 44; không lấy tốc độ liên kết Wi-Fi làm tốc độ Bluetooth.
- Có bản ghi thiết bị **Redmi Note 11 Pro**, **FPT Play Box+** cùng một số tai nghe Bluetooth.
- Danh sách AudioEndpoint được kiểm tra có Speakers/Headphones của Realtek và Speakers của FxSound Audio Enhancer; chưa thấy đầu ra Bluetooth trong kết quả đó.

Đây là kiểm kê thiết bị, không phải kết quả thử truyền âm thanh hoặc xác nhận tất cả thiết bị đang kết nối media. FxSound là một thành phần cần đưa vào ma trận tương thích khi thử nghiệm vì có thể tham gia đường định tuyến.

**Các phép thử quyết định trước khi triển khai đầy đủ**

| Thứ tự | Phép thử | Bằng chứng cần thu |
| --- | --- | --- |
| 1 | Redmi → PC → một đầu ra có dây | Nhận media, mở/dừng, kết nối lại, không im tiếng |
| 2 | FPT Play Box+ → PC → một đầu ra có dây | Có A2DP Source thực tế và phát được ứng dụng mục tiêu |
| 3 | Receiver → đầu ra ảo → thu lại | Thu được tín hiệu, không phát lặp hoặc vòng lặp |
| 4 | File âm thanh thử → hai đầu ra có dây/USB | Đo lệch ban đầu và lệch sau 30–60 phút |
| 5 | Một đầu ra dây + một Bluetooth; sau đó hai Bluetooth | Mở đồng thời được, đo độ trễ và gián đoạn |
| 6 | Hai nguồn Bluetooth cùng phát | Ghi riêng trạng thái kết nối, dữ liệu nghe được và khả năng tách nguồn |
| 7 | Nhận Bluetooth đồng thời phát Bluetooth | Đo độ ổn định radio khi chịu cả hai chiều |
| 8 | Rút/cắm, ngủ/thức, đổi đầu ra, bật microphone | Không treo toàn bộ ứng dụng; phục hồi và hiệu chỉnh lại |

30–60 phút là thời lượng kiểm thử đề xuất, không phải thời gian đã chạy. Các phép đo nên dùng tín hiệu thử và ghi âm nhiều kênh nếu có; nghe bằng tai chỉ là kiểm tra bổ sung. Trước khi cam kết mức đồng bộ, cần thống nhất mục đích nghe nhạc cùng phòng, nhiều phòng hay xem phim và chọn ngưỡng chấp nhận dựa trên số đo.

**Hướng triển khai đề xuất**

Lựa chọn ngôn ngữ đã chốt theo ưu tiên hiệu suất Windows: C++20 cho toàn bộ ứng dụng. Giao diện dùng Win32/Common Controls, receiver dùng C++/WinRT và đầu ra dùng WASAPI. Engine chạy tách tiến trình với UI, có bộ đệm riêng từng đầu ra, bù trễ, điều chỉnh lệch tốc độ và xử lý thiết bị mất kết nối. Chi tiết nằm trong [kế hoạch triển khai](IMPLEMENTATION_PLAN.vi.md).

Mốc đầu nên chứng minh đường nhận một nguồn và đường phát hai đầu ra, sau đó thử tích hợp qua đầu ra ảo. Chỉ mở rộng số thiết bị và tính năng nhiều nguồn sau khi các đường này vượt qua kiểm thử. Có thể triển khai sản phẩm hữu ích trên Windows; phạm vi phát hành phải gắn với cấu hình đã xác nhận thay vì hứa mọi tổ hợp thiết bị đều đồng bộ.

**Yêu cầu đã làm rõ và đối chiếu Nahimic Sound Sharing**

Người dùng xác nhận PC đóng vai trò thiết bị nhận Bluetooth: điện thoại hoặc TV box xuất media sang PC như xuất sang một loa. PC tiếp nhận rồi đưa âm thanh tới các tai nghe hoặc đầu ra được chọn. Ưu tiên nghe mượt, hạn chế ngắt tiếng; đồng bộ tương đối tốt nhất có thể.

Theo tài liệu công khai của Nahimic, Sound Sharing dùng Virtual Audio Device để nhân bản âm thanh ra hai đầu ra được chọn. Hướng SoundShare đề xuất có cùng nguyên lý định tuyến, không phải một giao thức Bluetooth mới thay thế Windows. Chưa kiểm tra nội bộ bản Nahimic cài trên máy và chưa đo so sánh. [Nahimic: Virtual Audio Device](https://nahimic.helprace.com/i766-i-can-t-hear-any-sound-on-my-system-after-using-sound-sharing).

Nahimic công bố tương thích tai nghe Bluetooth, USB và jack; tính năng được tối ưu cho tai nghe. Không suy ra thuật toán đồng bộ hay bộ đệm bên trong Nahimic từ mô tả sản phẩm. [Nahimic: thiết bị tương thích](https://nahimic.helprace.com/i764-which-headphone-is-compatible-with-sound-sharing-plus).

| Hạng mục | Nahimic theo tài liệu công khai | SoundShare đề xuất |
| --- | --- | --- |
| Định tuyến | Thiết bị ảo → hai đầu ra | Cùng nguyên lý; số đầu ra mở rộng theo khả năng đã thử |
| Nhận điện thoại/TV box | Tài liệu Sound Sharing được đọc không xác nhận chức năng receiver | Thêm lớp receiver Windows rồi chuyển tiếp qua bộ định tuyến |
| Chống ngắt và bù lệch | Không đủ tài liệu để mô tả thuật toán nội bộ | Chủ động thiết kế bộ đệm, bù lệch tốc độ và thống kê lỗi |
| Tốt hơn trên máy hiện tại | Chưa có kết quả đo so sánh | Cần thử cùng nguồn, thiết bị, khoảng cách và thời lượng |

Thiết kế ưu tiên độ mượt nên có bộ đệm riêng cho từng đầu ra; bổ sung dự trữ khi thiếu dữ liệu ngắn hạn; điều chỉnh tốc độ phát nhẹ để tránh bộ đệm cạn hoặc đầy dần; tách xử lý âm thanh khỏi công việc giao diện; và phục hồi riêng thiết bị gặp lỗi. Mọi điều chỉnh bộ đệm cần giới hạn và thực hiện êm để tránh tự tạo tiếng ngắt. Đây là mục tiêu triển khai, chưa phải tính năng đã hoàn thành. [Microsoft: Low Latency Audio](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio).

Phải phân biệt thiếu dữ liệu do ứng dụng xử lý trễ với mất dữ liệu trên đường truyền Bluetooth. Bộ đệm hấp thụ dao động ngắn nhưng không khôi phục nguyên vẹn dữ liệu đã mất. Với receiver Windows và các đầu ra WASAPI, ứng dụng không trực tiếp kiểm soát toàn bộ truyền lại gói tin Bluetooth; không nên hứa cơ chế tự truyền lại hay sửa mọi gói tin ở tầng ứng dụng. Bluetooth có cơ chế về truyền lại, độ trễ trình diễn và che lỗi tùy giao thức/thiết bị, nhưng các cơ chế này không đồng nghĩa không thể gián đoạn. [Bluetooth SIG: Auracast receiver rendering](https://www.bluetooth.com/wp-content/uploads/2024/05/2403_Auracast_Earbuds.pdf).

Đánh giá độ ổn định nên dựa trên số lần ngắt, tổng thời gian ngắt, lần bộ đệm thiếu dữ liệu và khả năng phục hồi khi chạy lâu. Với API PCM thông thường, những thống kê này không phải bộ đếm mất gói Bluetooth chính xác. Thử riêng chiều nhận, chiều phát, rồi cả hai chiều cùng lúc để tìm nơi gây lỗi. Tăng số đầu ra chỉ sau khi tổ hợp trước hoạt động ổn định.

LE Audio broadcast là lựa chọn giao thức mới hơn cho phần cứng tương thích, nhưng không thay thế đường định tuyến hỗn hợp tới các thiết bị dây, USB và Bluetooth Classic hiện có. Vì vậy khuyến nghị vẫn là backend WASAPI tương thích rộng, còn LE Audio tận dụng qua khả năng được Windows và thiết bị hỗ trợ; không mặc định có API ứng dụng để tùy ý phát broadcast tới số lượng bất kỳ.

**Đánh giá băng thông card Bluetooth hiện tại**

Đã xác định adapter Bluetooth thuộc 8852CE bằng metadata driver, nhưng chưa đo thông lượng và chưa thử nhận/phát media đồng thời. Có cơ sở để giữ card này cho bản thử nghiệm; chưa có cơ sở kết luận cần mua card mới hoặc cam kết số tai nghe tối đa.

Bluetooth Classic EDR có mức tốc độ vô tuyến 2 hoặc 3 Mb/s tùy chế độ; đó không phải băng thông âm thanh hữu dụng được bảo đảm, cũng không phải hai đường thu/phát độc lập cùng đạt tốc độ đó. Các luồng cần chia thời gian vô tuyến và còn có chi phí giao thức, điều khiển, truyền lại. [Bluetooth SIG: Technology Overview](https://www.bluetooth.com/learn-about-bluetooth/tech-overview/), [Bluetooth SIG: BR/EDR Radio](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core_v6.3/out/en/br-edr-controller/radio-physical-layer-specification.html).

Ví dụ tính tải, nếu giả sử mỗi luồng âm thanh nén là 350 kb/s, một nguồn nhận cộng hai tai nghe phát là ba luồng, tổng payload khoảng 1,05 Mb/s. Con số này chỉ cho thấy yêu cầu không vô lý xét theo tốc độ danh nghĩa; chưa tính overhead và không chứng minh controller/driver thực hiện được ba luồng media ổn định. Codec và bitrate thực tế phải được xác minh khi thử. Các đầu ra dây, USB hoặc HDMI không tạo thêm luồng âm thanh qua radio Bluetooth.

Wi-Fi hiện ở 5 GHz giúp tránh phần tải Wi-Fi của chính máy trên băng 2,4 GHz mà Bluetooth sử dụng, nhưng vẫn còn ảnh hưởng từ các thiết bị và mạng xung quanh. [Microsoft: Bluetooth và Wi-Fi coexistence](https://learn.microsoft.com/en-us/windows-hardware/drivers/bluetooth/bluetooth-faq).

Mốc hợp lý là thử một nguồn Bluetooth vào cộng một tai nghe Bluetooth ra, sau đó tăng lên hai tai nghe ra. Nhiều nguồn cùng phát hoặc từ ba tai nghe Bluetooth ra trở lên cần được đánh giá riêng, không suy từ phép cộng bitrate. Trước khi kết luận thiếu băng thông, phải phân biệt lỗi radio, giới hạn driver/profile và lỗi cấp dữ liệu âm thanh của ứng dụng.
