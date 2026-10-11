# MonitorSelection

Ứng dụng cấu hình bố cục đa màn hình (Multi-Monitor Layout & Property Manager) viết bằng **C (C11)** trên nền **SDL2** và **xrandr**, tuân thủ kiến trúc module hóa MDF và tiêu chuẩn mã nguồn trong [GEMINI.md](file:///home/fus/Documents/MonitorSelection/GEMINI.md).

---

## 1. Tính năng chính
- **Kéo thả & Hít nam châm thông minh (Anti-Overlap & Magnetic Snapping)**:
  - Các thẻ màn hình (`HDMI-1`, `DP-1`, `USB-C`) không bao giờ chồng lấn (overlap) lên nhau.
  - Tự động hít cạnh (edge snap) và hít góc/đỉnh (corner/vertex snap) khi khoảng cách nhỏ hơn ngưỡng `SnapThresholdPx` (`25 px`).
- **Tự động xác định màn hình gốc Top-Left `(0, 0)`**:
  - Tự động chọn màn hình nằm ở góc trên-trái nhất làm gốc tọa độ `POS(0, 0)`.
  - Tính toán tọa độ tương đối `POS(<x>, <y>)` theo thời gian thực cho toàn bộ các màn hình còn lại.
  - Hiển thị thông tin trực tiếp trên từng thẻ theo định dạng:
    ```text
    HDMI-1 | <W>x<H>@<Freq>
    POS(<x>, <Y>)
    ```
- **Bảng thuộc tính dọc bên phải (Right-Side Vertical Property Panel)**:
  - Khi click chọn một màn hình hoặc vừa kéo/thả (drag/drop) một màn hình, bảng thuộc tính bên phải (`Layer 2`) tự động hiển thị thông số chi tiết của màn hình đó.
  - Cho phép thay đổi trực tiếp độ phân giải (`< Res`, `Res >`) và tần số quét (`< Freq`, `Freq >`) từ danh sách mode hỗ trợ của màn hình.
- **Tích hợp `AppSysCall` (`xrandr` + YAML Config)**:
  - Tự động đọc cấu hình từ [UserConfig/app.yml](file:///home/fus/Documents/MonitorSelection/UserConfig/app.yml) và [UserConfig/disp.yml](file:///home/fus/Documents/MonitorSelection/UserConfig/disp.yml).
  - Nút **Apply**: Xác nhận qua hộp thoại Yes/No, lưu thông số vào `UserConfig/disp.yml`, và áp dụng cấu hình qua `SysCallXrandrApplySafe` (tự động hoàn tác nếu lỗi).
  - Nút **Roll-back**: Khôi phục ngay lập tức vị trí và độ phân giải/tần số quét của các màn hình về bản chụp (snapshot) trước đó.
- **Single-Instance & Multi-Thread**:
  - Chỉ cho phép 1 tiến trình chạy duy nhất; chạy lại `./AppExec` ở terminal thứ hai sẽ tự động ẩn/hiện (toggle) cửa sổ chính.
  - Kiến trúc 4 luồng (Main Thread, EventListenerThread, CardMonitorThread, MiscBackgroundThread) hoạt động theo cơ chế ngủ/đánh thức (`SDL_CondWait` / `SDL_WaitEvent`), không tốn CPU khi nhàn rỗi.

---

## 2. Cấu trúc thư mục & Tài liệu thiết kế (MDF)

| Đường dẫn | Mô tả | Tài liệu MDF |
| :--- | :--- | :--- |
| [main.c](file:///home/fus/Documents/MonitorSelection/main.c) | Điểm vào ứng dụng, khởi tạo 3 Layer UI, 4 luồng xử lý, và gắn kết sự kiện. | [mdf-app.md](file:///home/fus/Documents/MonitorSelection/mdf-app.md) |
| [AppCommon/](file:///home/fus/Documents/MonitorSelection/AppCommon) | Module nền tảng: kiểu dữ liệu, mã lỗi `Err_t`, hệ thống Log, `Color_t`, và hàng đợi vòng `CircleQueue_t`. | [mdf-app-common.md](file:///home/fus/Documents/MonitorSelection/AppCommon/mdf-app-common.md) |
| [AppSDL2/](file:///home/fus/Documents/MonitorSelection/AppSDL2) | Engine giao diện SDL2: `Window`, `Layer` (hỗ trợ xoay 0–360° & `RectClip`), `Component`, `Draw`, `Event`. | [mdf-app-sdl2.md](file:///home/fus/Documents/MonitorSelection/AppSDL2/mdf-app-sdl2.md) |
| [AppSysCall/](file:///home/fus/Documents/MonitorSelection/AppSysCall) | Giao tiếp hệ thống: chạy script/command (`Exec`), điều khiển `xrandr` (`Xrandr`), đọc/ghi YAML (`UserConfig`). | [mdf-app-syscall.md](file:///home/fus/Documents/MonitorSelection/AppSysCall/mdf-app-syscall.md) |
| [UserConfig/](file:///home/fus/Documents/MonitorSelection/UserConfig) | Chứa cấu hình giao diện [app.yml](file:///home/fus/Documents/MonitorSelection/UserConfig/app.yml) và cấu hình màn hình ưu tiên [disp.yml](file:///home/fus/Documents/MonitorSelection/UserConfig/disp.yml). | — |

---

## 3. Yêu cầu hệ thống & Cài đặt thư viện
Trên Ubuntu / Debian, cài đặt các gói phụ thuộc SDL2 và công cụ hệ thống:
```bash
sudo apt-get update
sudo apt-get install -y build-essential libsdl2-dev libsdl2-gfx-dev libsdl2-ttf-dev x11-xserver-utils doxygen
```

---

## 4. Hướng dẫn Biên dịch & Chạy ứng dụng

### Biên dịch toàn bộ dự án
Makefile ở thư mục gốc tự động phát hiện mọi thư mục con và biên dịch toàn bộ các file `*.c`:
```bash
make clean all
```

### Biên dịch từng module độc lập
```bash
make -C AppCommon clean all
make -C AppSDL2 clean all
make -C AppSysCall clean all
```

### Chạy ứng dụng
```bash
./AppExec
```
- **Kéo thả thẻ màn hình**: Dùng chuột trái kéo các thẻ `HDMI-1`, `DP-1`, `USB-C` trong khung bên trái. Khi thả chuột, thẻ sẽ tự động tránh đè nhau và hít cạnh/đỉnh vào thẻ gần nhất; thẻ góc trên-trái nhất tự động trở thành `POS(0, 0)`.
- **Đổi thông số màn hình**: Click hoặc kéo/thả một thẻ màn hình, sau đó nhấn các nút `< Res`, `Res >`, `< Freq`, `Freq >` ở bảng thuộc tính dọc bên phải.
- **Lưu & Áp dụng**: Nhấn nút **Apply** ở góc trên phải để lưu cấu hình vào `UserConfig/disp.yml` và áp dụng qua `xrandr`.
- **Hoàn tác**: Nhấn nút **Roll-back** để khôi phục lại bố cục và độ phân giải/tần số quét trước đó.
- **Thoát**: Nhấn phím `ESC` hoặc đóng cửa sổ.

### Sinh tài liệu Doxygen
```bash
make doc
```
Tài liệu HTML được xuất tại thư mục `docs/html/index.html`.

