# Kiểm thử trước khi đưa lên GitHub — SoundShare 0.2.1

Ngày: 02/11/2026. Mã nguồn được build và kiểm thử trên Windows x64, MSVC 19.29, Windows SDK 19041. Không thay đổi default endpoint, không mở receiver Bluetooth và không ghi nội dung PCM trong lượt kiểm thử này.

| Kiểm tra | Kết quả |
| --- | --- |
| Build Release x64 | Desktop, Worker, Ctl và các chương trình test đều build thành công |
| CTest core | Đạt 11 nhóm; gồm queue/concurrency, ramp, clock, drift mô phỏng một giờ, SRC, fan-out, recovery và PCM lỗi |
| CTest desktop_model | Đạt 11 nhóm; gồm cấu hình, giữ thiết bị, chặn feedback và thứ tự/hủy lệnh |
| CTest receiver_lifecycle | Đạt 4 nhóm mô phỏng receiver/transport; gồm mở nền, thử lại, hủy và giải phóng |
| CTest control_protocol | Đạt; validate, Unicode, staged config, pipe/shutdown và 32 yêu cầu từ tám client |
| AddressSanitizer | 11 nhóm core đạt; CRT static; không instrument toàn bộ dependency và không thay cho race detector |
| WASAPI probe | Đạt sáu probe render/loopback trên ba endpoint đang có |
| Stream Windows thật | 60,06 giây, loopback virtual → hai đầu ra Realtek, master/gain bằng 0; cả hai running |
| Bộ đếm trong lượt 60 giây | Hai đầu ra: 0 underrun, 0 dropped frames, 0 recovery; 0 timestamp error |
| Vòng đời engine | 100 chu kỳ start/stop thành công; đổi delay và remove/add nhánh thành công |
| CPU worker | Khoảng 4,32% của một lõi trong lượt đo này; tải nền không được cô lập, không dùng để suy ra tăng/giảm hiệu suất |

Nguồn loopback ở trạng thái idle; engine cấp silence có thống kê riêng. Test mute xác nhận vòng đời stream và bộ đệm, không chứng minh chất lượng nghe, độ lệch âm học hoặc không mất gói Bluetooth. Snapshot đầy đủ nằm trong `test-results/prepublish-windows.json` trên máy phát triển và được loại khỏi Git vì chứa ID thiết bị.

Các phép thử còn cần thiết bị thực: điện thoại và TV box cùng truyền media, hai tai nghe Bluetooth đồng thời, nghe/chạy dài, mất sóng, sleep/resume và đo đồng bộ âm học. Xem [kế hoạch kiểm thử](../TEST_PLAN.vi.md) và [báo cáo rà soát](AUDIT_0.2.1.vi.md).

GitHub có workflow `Windows build and tests` trên Windows Server 2022/VS 2022 để kiểm tra lại build và bốn bộ CTest. Workflow này không chạy thử media Bluetooth hay phát âm thanh trên runner.
