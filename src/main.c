#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

typedef struct {
    int region_id;
    int aqi;
    double pm25;
    long timestamp;
} AlertNode;

typedef struct {
    AlertNode *tree;     
    int size;
    int capacity;
} AlertHeap;

typedef struct {
    int region_id;
    int aqi;
    double pm25;
    long timestamp;
} SensorReading;

typedef struct BSTNode {
    int region_id;
    int aqi_val;
    double pm25_val;
    long timestamp;
    long reading_count;
    long sum_aqi;
    int peak_aqi;
    struct BSTNode *left;
    struct BSTNode *right;
} BSTNode;

const char *REGION_NAMES[30] = {
    "Colaba (South Mumbai)", "Marine Drive / Churchgate", "Fort / CST", "Malabar Hill",
    "Worli", "Lower Parel", "Dadar", "Prabhadevi", "Mahim", "Bandra West",
    "BKC (Bandra Kurla)", "Khar West", "Santacruz", "Vile Parle", "Andheri West",
    "Andheri East", "Juhu", "Jogeshwari", "Goregaon", "Malad West", "Kandivali",
    "Borivali", "Dahisar", "Kurla", "Ghatkopar", "Vidyavihar", "Bhandup", "Mulund",
    "Chembur", "Powai / Chandivali"
};

typedef struct {
    AlertHeap heap;
    BSTNode *bst_root;

    long total_records;
    long long sum_aqi_all;
    int peak_aqi_all;

    double history[90];
    int history_count;

    BOOL region_active[30];

    BOOL running;
    UINT_PTR timer_id;
    int interval_ms;

    HWND hwnd_main;
    HWND hwnd_header;
    HWND hwnd_tab;
    HWND hwnd_status;

    HWND hwnd_panel_dash;
    HWND hwnd_panel_alerts;
    HWND hwnd_panel_archive;
    HWND hwnd_panel_reports;

    HWND hwnd_lbl_kpi_total;
    HWND hwnd_lbl_kpi_alerts;
    HWND hwnd_lbl_kpi_regions;
    HWND hwnd_lbl_kpi_avg;
    HWND hwnd_lbl_kpi_peak;

    HWND hwnd_sparkline;
    HWND hwnd_lv_recent;

    HWND hwnd_lv_alerts;

    HWND hwnd_combo_search;
    HWND hwnd_lbl_search_details;
    HWND hwnd_edit_search_info;
    HWND hwnd_bst_tree;
    HWND hwnd_lbl_bst_step;
    const BSTNode *bst_search_path[30];
    int bst_search_path_count;
    int bst_search_step;
    int bst_search_target;
    const BSTNode *bst_search_result;
    int bst_tree_scroll_x;
    int bst_tree_scroll_y;
    int bst_tree_zoom_percent;

    HWND hwnd_lv_report;
    HWND hwnd_lbl_report_stats;

    HWND hwnd_btn_start;
    HWND hwnd_btn_stop;

    HWND toast_hwnds[5];
    int toast_count;

    HFONT font_title;
    HFONT font_sub;
    HFONT font_body;
    HFONT font_bold;
    HFONT font_kpi_digit;

    HBRUSH brush_bg;
    HBRUSH brush_card;
    HBRUSH brush_header;
} AppState;

static AppState g_app;

/* dsa */

static void swap_nodes(AlertNode *a, AlertNode *b) {
    AlertNode temp = *a;
    *a = *b;
    *b = temp;
}

void heap_init(AlertHeap *heap) {
    heap->capacity = 64;
    heap->tree = (AlertNode *)calloc((size_t)heap->capacity, sizeof(AlertNode));
    heap->size = 0;
}

void heap_free(AlertHeap *heap) {
    free(heap->tree);
    heap->tree = NULL;
    heap->size = 0;
    heap->capacity = 0;
}

int heap_is_empty(const AlertHeap *heap) {
    return heap->size <= 0;
}

static int heap_ensure_capacity(AlertHeap *heap) {
    if (heap->size < heap->capacity) return 1;
    int new_capacity = heap->capacity * 2;
    AlertNode *grown = (AlertNode *)realloc(heap->tree, (size_t)new_capacity * sizeof(AlertNode));
    if (!grown) return 0; 
    heap->tree = grown;
    heap->capacity = new_capacity;
    return 1;
}

static void sift_up(AlertHeap *heap, int i) {
    while (i != 0 && heap->tree[(i - 1) / 2].aqi < heap->tree[i].aqi) {
        swap_nodes(&heap->tree[i], &heap->tree[(i - 1) / 2]);
        i = (i - 1) / 2;
    }
}

static void max_heapify(AlertHeap *heap, int idx) {
    int largest = idx;
    int left = 2 * idx + 1;
    int right = 2 * idx + 2;

    if (left < heap->size && heap->tree[left].aqi > heap->tree[largest].aqi) largest = left;
    if (right < heap->size && heap->tree[right].aqi > heap->tree[largest].aqi) largest = right;

    if (largest != idx) {
        swap_nodes(&heap->tree[idx], &heap->tree[largest]);
        max_heapify(heap, largest);
    }
}

void heap_insert(AlertHeap *heap, int region_id, int aqi, double pm25, long timestamp) {
    if (!heap_ensure_capacity(heap)) return;

    int i = heap->size;
    heap->tree[i].region_id = region_id;
    heap->tree[i].aqi = aqi;
    heap->tree[i].pm25 = pm25;
    heap->tree[i].timestamp = timestamp;
    heap->size++;

    sift_up(heap, i);
}

AlertNode heap_extract_max(AlertHeap *heap) {
    if (heap_is_empty(heap)) {
        AlertNode empty_node = { -1, -1, 0.0, 0 };
        return empty_node;
    }
    AlertNode max_alert = heap->tree[0];
    heap->tree[0] = heap->tree[heap->size - 1];
    heap->size--;
    max_heapify(heap, 0);
    return max_alert;
}

static BSTNode *create_node(int region_id, int aqi_val, double pm25_val, long timestamp) {
    BSTNode *node = (BSTNode *)malloc(sizeof(BSTNode));
    node->region_id = region_id;
    node->aqi_val = aqi_val;
    node->pm25_val = pm25_val;
    node->timestamp = timestamp;
    node->reading_count = 1;
    node->sum_aqi = aqi_val;
    node->peak_aqi = aqi_val;
    node->left = NULL;
    node->right = NULL;
    return node;
}

BSTNode *bst_insert_or_update(BSTNode *root, int region_id, int aqi_val, double pm25_val, long timestamp) {
    if (root == NULL) return create_node(region_id, aqi_val, pm25_val, timestamp);

    if (region_id == root->region_id) {
        root->aqi_val = aqi_val;
        root->pm25_val = pm25_val;
        root->timestamp = timestamp;
        root->reading_count++;
        root->sum_aqi += aqi_val;
        if (aqi_val > root->peak_aqi) root->peak_aqi = aqi_val;
        return root;
    }

    if (region_id < root->region_id) {
        root->left = bst_insert_or_update(root->left, region_id, aqi_val, pm25_val, timestamp);
    } else {
        root->right = bst_insert_or_update(root->right, region_id, aqi_val, pm25_val, timestamp);
    }
    return root;
}

static BSTNode *bst_search_trace(BSTNode *root, int target_region_id,
                                 const BSTNode **path, int *path_count) {
    *path_count = 0;
    while (root && *path_count < 30) {
        path[(*path_count)++] = root;
        if (root->region_id == target_region_id) return root;
        if (target_region_id < root->region_id) {
            root = root->left;
        } else {
            root = root->right;
        }
    }
    return NULL;
}

static void bst_inorder(const BSTNode *root, const BSTNode *nodes[], int *count) {
    if (root == NULL || *count >= 30) {
        return;
    }

    bst_inorder(root->left, nodes, count);
    if (*count < 30) {
        nodes[*count] = root;
        (*count)++;
    }
    bst_inorder(root->right, nodes, count);
}

int bst_count(const BSTNode *root) {
    if (root == NULL) return 0;
    return 1 + bst_count(root->left) + bst_count(root->right);
}

int bst_height(const BSTNode *root) {
    if (root == NULL) return 0;
    int left_h = bst_height(root->left);
    int right_h = bst_height(root->right);
    if (left_h > right_h) {
        return 1 + left_h;
    }
    return 1 + right_h;
}

void bst_free(BSTNode *root) {
    if (root != NULL) {
        bst_free(root->left);
        bst_free(root->right);
        free(root);
    }
}

/* gui */

const char *sensor_region_name(int region_id) {
    if (region_id >= 1 && region_id <= 30) return REGION_NAMES[region_id - 1];
    return "Unknown Region";
}

const char *sensor_aqi_category(int aqi) {
    if (aqi <= 50) return "Good";
    if (aqi <= 100) return "Satisfactory";
    if (aqi <= 200) return "Moderate";
    if (aqi <= 300) return "Poor";
    if (aqi <= 400) return "Very Poor";
    return "Severe";
}

static double random_double(double min_val, double max_val) {
    double scale = (double)rand() / (double)RAND_MAX;
    return min_val + scale * (max_val - min_val);
}

void sensor_generate_reading(SensorReading *out_reading) {
    int region_id = (rand() % 30) + 1;
    double pm25;
    int spike_roll = rand() % 100;

    if (spike_roll < 15) pm25 = random_double(230.0, 380.0);
    else if (spike_roll < 40) pm25 = random_double(80.0, 180.0);
    else pm25 = random_double(10.0, 80.0);

    int aqi = (int)(pm25 * 1.35);
    if (aqi > 500) aqi = 500;

    out_reading->region_id = region_id;
    out_reading->pm25 = pm25;
    out_reading->aqi = aqi;
    out_reading->timestamp = (long)time(NULL);
}

static void format_time_label(long timestamp, char *buffer, size_t buffer_size) {
    time_t raw = (time_t)timestamp;
    struct tm *info = localtime(&raw);
    if (info) strftime(buffer, buffer_size, "%H:%M:%S", info);
    else snprintf(buffer, buffer_size, "--:--:--");
}

static void push_history(AppState *s, double aqi) {
    if (s->history_count < 90) {
        s->history[s->history_count++] = aqi;
    } else {
        for (int i = 1; i < 90; i++) s->history[i - 1] = s->history[i];
        s->history[90 - 1] = aqi;
    }
}

static void update_kpi_labels(AppState *s) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%ld", s->total_records);
    SetWindowText(s->hwnd_lbl_kpi_total, buf);

    snprintf(buf, sizeof(buf), "%d", s->heap.size);
    SetWindowText(s->hwnd_lbl_kpi_alerts, buf);

    snprintf(buf, sizeof(buf), "%d", bst_count(s->bst_root));
    SetWindowText(s->hwnd_lbl_kpi_regions, buf);

    double avg = 0.0;
    if (s->total_records > 0) {
        avg = (double)s->sum_aqi_all / (double)s->total_records;
    }
    snprintf(buf, sizeof(buf), "%.1f", avg);
    SetWindowText(s->hwnd_lbl_kpi_avg, buf);

    snprintf(buf, sizeof(buf), "%d", s->peak_aqi_all);
    SetWindowText(s->hwnd_lbl_kpi_peak, buf);

    const char *feed_status = "PAUSED";
    if (s->running) {
        feed_status = "STREAM ACTIVE";
    }
    snprintf(buf, sizeof(buf), " Status: %s  |  Total Ingested: %ld  |  Alert Heap Size: %d  |  Active BST Nodes: %d",
             feed_status,
             s->total_records, s->heap.size, bst_count(s->bst_root));
    SetWindowText(s->hwnd_status, buf);

    InvalidateRect(s->hwnd_header, NULL, FALSE);
}

static void add_listview_row(HWND hwnd_lv, const char* col0, const char* col1, const char* col2, const char* col3, const char* col4) {
    LVITEM lvi = { 0 };
    lvi.mask = LVIF_TEXT;
    lvi.iItem = 0;
    lvi.pszText = (LPSTR)col0;
    ListView_InsertItem(hwnd_lv, &lvi);

    if (col1) ListView_SetItemText(hwnd_lv, 0, 1, (LPSTR)col1);
    if (col2) ListView_SetItemText(hwnd_lv, 0, 2, (LPSTR)col2);
    if (col3) ListView_SetItemText(hwnd_lv, 0, 3, (LPSTR)col3);
    if (col4) ListView_SetItemText(hwnd_lv, 0, 4, (LPSTR)col4);
}

static int ci_strcmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static void refresh_alerts_listview(AppState *s) {
    ListView_DeleteAllItems(s->hwnd_lv_alerts);
    if (heap_is_empty(&s->heap)) return;

    int n = s->heap.size;
    AlertNode *sorted = (AlertNode *)malloc((size_t)n * sizeof(AlertNode));
    if (!sorted) return;
    memcpy(sorted, s->heap.tree, (size_t)n * sizeof(AlertNode));
    AlertHeap sorted_heap;
    sorted_heap.tree = sorted;
    sorted_heap.size = n;
    sorted_heap.capacity = n;

    int count = 0;
    while (!heap_is_empty(&sorted_heap)) {
        AlertNode alert = heap_extract_max(&sorted_heap);
        const char *rname = sensor_region_name(alert.region_id);

        char time_buf[16], pm_buf[32], aqi_buf[32];
        format_time_label(alert.timestamp, time_buf, sizeof(time_buf));
        snprintf(pm_buf, sizeof(pm_buf), "%.1f", alert.pm25);
        snprintf(aqi_buf, sizeof(aqi_buf), "%d", alert.aqi);

        LVITEM lvi = { 0 };
        lvi.mask = LVIF_TEXT;
        lvi.iItem = count;
        lvi.pszText = time_buf;
        int idx = ListView_InsertItem(s->hwnd_lv_alerts, &lvi);

        ListView_SetItemText(s->hwnd_lv_alerts, idx, 1, (LPSTR)rname);
        ListView_SetItemText(s->hwnd_lv_alerts, idx, 2, aqi_buf);
        ListView_SetItemText(s->hwnd_lv_alerts, idx, 3, pm_buf);
        ListView_SetItemText(s->hwnd_lv_alerts, idx, 4, (LPSTR)sensor_aqi_category(alert.aqi));

        count++;
    }
    free(sorted_heap.tree);
}

typedef struct {
    char line1[128];
    char line2[96];
    COLORREF accent;
} ToastData;

static LRESULT CALLBACK ToastWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void toast_reposition_all(void) {
    RECT work;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
    const int margin = 16, gap = 10, tw = 320, th = 78;
    int y = work.bottom - margin;

    for (int i = g_app.toast_count - 1; i >= 0; i--) {
        y -= th;
        if (g_app.toast_hwnds[i]) {
            SetWindowPos(g_app.toast_hwnds[i], HWND_TOPMOST,
                         work.right - margin - tw, y, tw, th, SWP_NOACTIVATE);
        }
        y -= gap;
    }
}

static void toast_remove(HWND hwnd) {
    int idx = -1;
    for (int i = 0; i < g_app.toast_count; i++) {
        if (g_app.toast_hwnds[i] == hwnd) { idx = i; break; }
    }
    if (idx == -1) return;
    for (int i = idx; i < g_app.toast_count - 1; i++) g_app.toast_hwnds[i] = g_app.toast_hwnds[i + 1];
    g_app.toast_count--;
    toast_reposition_all();
}

static void show_toast_alert(const char *region_name, int region_id, int aqi, double pm25, const char *category) {
    if (g_app.toast_count >= 5) {
        DestroyWindow(g_app.toast_hwnds[0]); 
    }

    ToastData *data = (ToastData *)malloc(sizeof(ToastData));
    if (!data) return;
    snprintf(data->line1, sizeof(data->line1), "%s  |  AQI %d (%s)", region_name, aqi, category);
    snprintf(data->line2, sizeof(data->line2), "PM2.5 %.1f ug/m3   -   Region ID %d", pm25, region_id);
    data->accent = RGB(225, 29, 72);
    if (aqi > 400) {
        data->accent = RGB(159, 18, 57);
    }

    RECT work;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
    const int tw = 320, th = 78, margin = 16;

    HWND toast = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, "ToastClass", "",
                                 WS_POPUP | WS_VISIBLE,
                                 work.right - margin - tw, work.bottom - margin - th, tw, th,
                                 NULL, NULL, NULL, data);

    if (toast) {
        g_app.toast_hwnds[g_app.toast_count++] = toast;
        toast_reposition_all();
        SetTimer(toast, 1, 4500, NULL);
    } else {
        free(data);
    }
}

static void ingest_reading(AppState *s, SensorReading reading) {
    char time_buf[16], pm_buf[32], aqi_buf[32];
    format_time_label(reading.timestamp, time_buf, sizeof(time_buf));
    snprintf(pm_buf, sizeof(pm_buf), "%.1f", reading.pm25);
    snprintf(aqi_buf, sizeof(aqi_buf), "%d", reading.aqi);

    add_listview_row(s->hwnd_lv_recent, time_buf, sensor_region_name(reading.region_id), pm_buf, aqi_buf, sensor_aqi_category(reading.aqi));

    if (ListView_GetItemCount(s->hwnd_lv_recent) > 200) {
        ListView_DeleteItem(s->hwnd_lv_recent, ListView_GetItemCount(s->hwnd_lv_recent) - 1);
    }

    s->bst_root = bst_insert_or_update(s->bst_root, reading.region_id, reading.aqi, reading.pm25, reading.timestamp);
    if (s->hwnd_bst_tree) InvalidateRect(s->hwnd_bst_tree, NULL, FALSE);

    if (!s->region_active[reading.region_id - 1]) {
        s->region_active[reading.region_id - 1] = TRUE;

        char combo_label[128];
        snprintf(combo_label, sizeof(combo_label), "%s (ID: %d)", sensor_region_name(reading.region_id), reading.region_id);

        int idx = (int)SendMessage(s->hwnd_combo_search, CB_ADDSTRING, 0, (LPARAM)combo_label);
        SendMessage(s->hwnd_combo_search, CB_SETITEMDATA, idx, (LPARAM)reading.region_id);

        if (SendMessage(s->hwnd_combo_search, CB_GETCURSEL, 0, 0) == CB_ERR) {
            SendMessage(s->hwnd_combo_search, CB_SETCURSEL, 0, 0);
        }
    }

    s->total_records++;
    s->sum_aqi_all += reading.aqi;
    if (reading.aqi > s->peak_aqi_all) s->peak_aqi_all = reading.aqi;

    push_history(s, (double)reading.aqi);
    InvalidateRect(s->hwnd_sparkline, NULL, FALSE);

    if (reading.aqi > 300) {
        heap_insert(&s->heap, reading.region_id, reading.aqi, reading.pm25, reading.timestamp);
        refresh_alerts_listview(s);

        PlaySound(TEXT("SystemExclamation"), NULL, SND_ALIAS | SND_ASYNC);
        MessageBeep(MB_OK);
        show_toast_alert(sensor_region_name(reading.region_id), reading.region_id,
                          reading.aqi, reading.pm25, sensor_aqi_category(reading.aqi));
    }

    update_kpi_labels(s);
}

static LRESULT CALLBACK SparklineWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rect;
            GetClientRect(hwnd, &rect);

            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBM = CreateCompatibleBitmap(hdc, width, height);
            HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);

            FillRect(memDC, &rect, g_app.brush_card);

            HPEN gridPen = CreatePen(PS_DOT, 1, RGB(226, 232, 240));
            HGDIOBJ oldPen = SelectObject(memDC, gridPen);
            for (int y_val = 100; y_val <= 400; y_val += 100) {
                int gy = height - (int)(((double)y_val / 500.0) * (height - 10));
                MoveToEx(memDC, 0, gy, NULL);
                LineTo(memDC, width, gy);
            }
            SelectObject(memDC, oldPen);
            DeleteObject(gridPen);

            HPEN threshPen = CreatePen(PS_DASH, 1, RGB(225, 29, 72));
            oldPen = SelectObject(memDC, threshPen);
            int thresh_y = height - (int)(((double)300 / 500.0) * (height - 10));
            MoveToEx(memDC, 0, thresh_y, NULL);
            LineTo(memDC, width, thresh_y);
            SelectObject(memDC, oldPen);
            DeleteObject(threshPen);

            if (g_app.history_count >= 2) {
                POINT pts[90 + 2];
                double step_x = (double)width / (double)(90 - 1);

                for (int i = 0; i < g_app.history_count; i++) {
                    double val = g_app.history[i];
                    pts[i].x = (int)(i * step_x);
                    pts[i].y = (height - 5) - (int)((val / 500.0) * (height - 10));
                }

                pts[g_app.history_count].x = (int)((g_app.history_count - 1) * step_x);
                pts[g_app.history_count].y = height;
                pts[g_app.history_count + 1].x = 0;
                pts[g_app.history_count + 1].y = height;

                HBRUSH fillBrush = CreateSolidBrush(RGB(239, 246, 255));
                HPEN nullPen = CreatePen(PS_NULL, 0, 0);
                SelectObject(memDC, fillBrush);
                SelectObject(memDC, nullPen);
                Polygon(memDC, pts, g_app.history_count + 2);
                DeleteObject(fillBrush);
                DeleteObject(nullPen);

                HPEN linePen = CreatePen(PS_SOLID, 2, RGB(37, 99, 235));
                oldPen = SelectObject(memDC, linePen);
                Polyline(memDC, pts, g_app.history_count);
                SelectObject(memDC, oldPen);
                DeleteObject(linePen);
            }

            FrameRect(memDC, &rect, (HBRUSH)GetStockObject(DC_BRUSH));

            BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBM);
            DeleteObject(memBM);
            DeleteDC(memDC);

            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK AQIPanelWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rect;
            GetClientRect(hwnd, &rect);

            HBRUSH bgBrush = CreateSolidBrush(RGB(255, 255, 255));
            FillRect(hdc, &rect, bgBrush);
            DeleteObject(bgBrush);

            HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(226, 232, 240));
            HGDIOBJ oldPen = SelectObject(hdc, borderPen);
            HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            RoundRect(hdc, 0, 0, rect.right, rect.bottom, 18, 18);
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(borderPen);

            EndPaint(hwnd, &ps);
            return 0;
        }
    }

    if (msg == WM_COMMAND || msg == WM_NOTIFY) {
        return SendMessage(GetParent(hwnd), msg, wParam, lParam);
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK HeaderWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rect;
            GetClientRect(hwnd, &rect);

            TRIVERTEX vert[2] = {
                {0, 0, 0x0000, 0x0000, 0x0000, 0x0000},
                {rect.right, rect.bottom, 0x00, 0x00, 0x00, 0x0000}
            };
            GRADIENT_RECT gRect = {0, 1};
            vert[0].Red = 0x0A00;
            vert[0].Green = 0x1D00;
            vert[0].Blue = 0x2A00;
            vert[0].Alpha = 0x0000;
            vert[1].Red = 0x0D00;
            vert[1].Green = 0x1C00;
            vert[1].Blue = 0x2B00;
            vert[1].Alpha = 0x0000;
            GradientFill(hdc, vert, 2, &gRect, 1, GRADIENT_FILL_RECT_H);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));
            SelectObject(hdc, g_app.font_title);
            TextOutA(hdc, 20, 12, "AEROVISION MUMBAI", 17);

            SelectObject(hdc, g_app.font_sub);
            SetTextColor(hdc, RGB(148, 163, 184));
            TextOutA(hdc, 220, 17, "Enterprise AQI Telemetry & Emergency Alert Platform", 51);

            COLORREF status_color = RGB(100, 116, 139);
            const char *status_text = "OFFLINE";
            int status_text_length = 7;
            if (g_app.running) {
                status_color = RGB(16, 185, 129);
                status_text = "STREAM LIVE";
                status_text_length = 11;
            }
            HBRUSH dotBrush = CreateSolidBrush(status_color);
            HGDIOBJ oldB = SelectObject(hdc, dotBrush);
            HPEN nullP = CreatePen(PS_NULL, 0, 0);
            HGDIOBJ oldP = SelectObject(hdc, nullP);

            Ellipse(hdc, rect.right - 140, 18, rect.right - 128, 30);

            SelectObject(hdc, g_app.font_bold);
            SetTextColor(hdc, status_color);
            TextOutA(hdc, rect.right - 120, 15, status_text, status_text_length);

            SelectObject(hdc, oldB);
            SelectObject(hdc, oldP);
            DeleteObject(dotBrush);
            DeleteObject(nullP);

            HPEN linePen = CreatePen(PS_SOLID, 1, RGB(59, 130, 246));
            HGDIOBJ oldLine = SelectObject(hdc, linePen);
            MoveToEx(hdc, 0, rect.bottom - 1, NULL);
            LineTo(hdc, rect.right, rect.bottom - 1);
            SelectObject(hdc, oldLine);
            DeleteObject(linePen);

            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK ToastWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rect;
            GetClientRect(hwnd, &rect);
            ToastData *data = (ToastData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

            HBRUSH bgBrush = CreateSolidBrush(RGB(255, 255, 255));
            FillRect(hdc, &rect, bgBrush);
            DeleteObject(bgBrush);

            if (data) {
                RECT accentRect = { rect.left, rect.top, rect.left + 6, rect.bottom };
                HBRUSH accentBrush = CreateSolidBrush(data->accent);
                FillRect(hdc, &accentRect, accentBrush);
                DeleteObject(accentBrush);

                SetBkMode(hdc, TRANSPARENT);
                RECT text1 = { 18, 10, rect.right - 10, 42 };
                SelectObject(hdc, g_app.font_bold);
                SetTextColor(hdc, RGB(15, 23, 42));
                DrawText(hdc, data->line1, -1, &text1, DT_LEFT | DT_WORDBREAK);

                RECT text2 = { 18, 44, rect.right - 10, 70 };
                SelectObject(hdc, g_app.font_sub);
                SetTextColor(hdc, RGB(100, 116, 139));
                DrawText(hdc, data->line2, -1, &text2, DT_LEFT | DT_WORDBREAK);
            }

            FrameRect(hdc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN:
            DestroyWindow(hwnd); 
            return 0;

        case WM_TIMER:
            KillTimer(hwnd, wParam);
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY: {
            ToastData *data = (ToastData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
            if (data) free(data);
            toast_remove(hwnd);
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

typedef struct {
    int col;
    BOOL ascending;
} SortState;

static SortState g_sort_recent = { -1, TRUE };
static SortState g_sort_alerts = { -1, TRUE };
static SortState g_sort_report = { -1, TRUE };

static BOOL is_numeric_column(HWND list, int column) {
    if (list == g_app.hwnd_lv_recent) {
        return column == 2 || column == 3;
    }
    if (list == g_app.hwnd_lv_alerts) {
        return column == 2 || column == 3;
    }
    if (list == g_app.hwnd_lv_report) {
        return column == 0 || column == 2 || column == 4 ||
               column == 5 || column == 6;
    }
    return FALSE;
}

static int compare_listview_rows(HWND list, int first_row, int second_row,
                                 int column, BOOL ascending) {
    char first_text[128];
    char second_text[128];
    int result;

    ListView_GetItemText(list, first_row, column, first_text, sizeof(first_text));
    ListView_GetItemText(list, second_row, column, second_text, sizeof(second_text));

    if (is_numeric_column(list, column)) {
        double first_number = atof(first_text);
        double second_number = atof(second_text);
        if (first_number < second_number) {
            result = -1;
        } else if (first_number > second_number) {
            result = 1;
        } else {
            result = 0;
        }
    } else {
        result = ci_strcmp(first_text, second_text);
    }

    if (!ascending) {
        result = -result;
    }
    return result;
}

static void swap_listview_rows(HWND list, int first_row, int second_row) {
    char first_values[8][128];
    char second_values[8][128];
    int column_count = 5;
    UINT first_state;
    UINT second_state;
    UINT state_mask = LVIS_SELECTED | LVIS_FOCUSED;

    if (list == g_app.hwnd_lv_report) {
        column_count = 8;
    }

    first_state = ListView_GetItemState(list, first_row, state_mask);
    second_state = ListView_GetItemState(list, second_row, state_mask);

    for (int column = 0; column < column_count; column++) {
        ListView_GetItemText(list, first_row, column, first_values[column],
                             sizeof(first_values[column]));
        ListView_GetItemText(list, second_row, column, second_values[column],
                             sizeof(second_values[column]));
    }

    for (int column = 0; column < column_count; column++) {
        ListView_SetItemText(list, first_row, column, second_values[column]);
        ListView_SetItemText(list, second_row, column, first_values[column]);
    }

    ListView_SetItemState(list, first_row, second_state, state_mask);
    ListView_SetItemState(list, second_row, first_state, state_mask);
}

static void handle_column_click(HWND hwndLV, int col) {
    SortState *state;
    if (hwndLV == g_app.hwnd_lv_recent) state = &g_sort_recent;
    else if (hwndLV == g_app.hwnd_lv_alerts) state = &g_sort_alerts;
    else if (hwndLV == g_app.hwnd_lv_report) state = &g_sort_report;
    else return;

    if (state->col == col) state->ascending = !state->ascending;
    else { state->col = col; state->ascending = TRUE; }

    int item_count = ListView_GetItemCount(hwndLV);
    for (int i = 0; i < item_count - 1; i++) {
        for (int row = 0; row < item_count - i - 1; row++) {
            int comparison = compare_listview_rows(hwndLV, row, row + 1,
                                                   col, state->ascending);
            if (comparison > 0) {
                swap_listview_rows(hwndLV, row, row + 1);
            }
        }
    }
}

static void apply_unified_theme(HWND hwnd, HFONT font) {
    if (!hwnd) return;
    SetWindowTheme(hwnd, L"Explorer", NULL);
    if (font) SendMessage(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
}

static void style_listview_modern(HWND hwnd_lv) {
    if (!hwnd_lv) return;
    SetWindowTheme(hwnd_lv, L"Explorer", NULL);
    DWORD ex_style = LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP;
    ListView_SetExtendedListViewStyleEx(hwnd_lv, ex_style, ex_style);
    ListView_SetBkColor(hwnd_lv, RGB(255, 255, 255));
    ListView_SetTextBkColor(hwnd_lv, RGB(255, 255, 255));
    ListView_SetTextColor(hwnd_lv, RGB(15, 23, 42));
    ListView_SetOutlineColor(hwnd_lv, RGB(226, 232, 240));
}

static void create_listview_columns(HWND hwnd_lv, const char* cols[], int count) {
    LVCOLUMN lvc = { 0 };
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    for (int i = 0; i < count; i++) {
        lvc.iSubItem = i;
        lvc.pszText = (LPSTR)cols[i];
        lvc.cx = 135;
        ListView_InsertColumn(hwnd_lv, i, &lvc);
    }

    HWND header = (HWND)SendMessage(hwnd_lv, LVM_GETHEADER, 0, 0);
    if (header) {
        SetWindowTheme(header, L"Explorer", NULL);
        SendMessage(header, WM_SETFONT, (WPARAM)g_app.font_bold, TRUE);
    }
}

typedef struct {
    const BSTNode *node;
    int depth;
    int x;
    int y;
} BSTLayoutEntry;

static void collect_bst_layout(const BSTNode *node, int depth, BSTLayoutEntry entries[], int *count) {
    if (!node || *count >= 30) return;
    collect_bst_layout(node->left, depth + 1, entries, count);
    if (*count < 30) {
        entries[*count].node = node;
        entries[*count].depth = depth;
        (*count)++;
    }
    collect_bst_layout(node->right, depth + 1, entries, count);
}

static int find_bst_layout_entry(const BSTLayoutEntry entries[], int count, const BSTNode *node) {
    for (int i = 0; i < count; i++) {
        if (entries[i].node == node) return i;
    }
    return -1;
}

static int find_bst_search_step(const BSTNode *node) {
    for (int i = 0; i < g_app.bst_search_path_count; i++) {
        if (g_app.bst_search_path[i] == node) return i;
    }
    return -1;
}

static int scale_bst_value(int value) {
    return (value * g_app.bst_tree_zoom_percent + 50) / 100;
}

static BOOL bst_search_edge_visited(const BSTNode *parent, const BSTNode *child) {
    int last = g_app.bst_search_step;
    if (last >= g_app.bst_search_path_count) last = g_app.bst_search_path_count - 1;
    for (int i = 1; i <= last; i++) {
        if (g_app.bst_search_path[i - 1] == parent && g_app.bst_search_path[i] == child) return TRUE;
    }
    return FALSE;
}

static void update_bst_scrollbars(HWND hwnd, const BSTLayoutEntry entries[], int count) {
    RECT client;
    GetClientRect(hwnd, &client);
    int max_depth = 0;
    for (int i = 0; i < count; i++) {
        if (entries[i].depth > max_depth) max_depth = entries[i].depth;
    }

    int content_width = client.right;
    if (count > 0) {
        content_width = scale_bst_value(80 + (count - 1) * 88);
    }
    if (content_width < client.right) content_width = client.right;
    int content_height = client.bottom;
    if (count > 0) {
        content_height = scale_bst_value(76 + max_depth * 82);
    }
    int page_height = 1;
    if (client.bottom > 40) {
        page_height = client.bottom - 40;
    }
    if (content_height < page_height) content_height = page_height;

    SCROLLINFO si = { 0 };
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = content_width - 1;
    si.nPage = (UINT)client.right;
    if (si.nPage == 0) {
        si.nPage = 1;
    }
    si.nPos = g_app.bst_tree_scroll_x;
    SetScrollInfo(hwnd, SB_HORZ, &si, TRUE);
    GetScrollInfo(hwnd, SB_HORZ, &si);
    g_app.bst_tree_scroll_x = si.nPos;

    si.nMax = content_height - 1;
    si.nPage = (UINT)page_height;
    si.nPos = g_app.bst_tree_scroll_y;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
    GetScrollInfo(hwnd, SB_VERT, &si);
    g_app.bst_tree_scroll_y = si.nPos;
}

static void scroll_bst_tree(HWND hwnd, int bar, UINT request) {
    SCROLLINFO si = { 0 };
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    if (!GetScrollInfo(hwnd, bar, &si)) return;

    int position = si.nPos;
    int page = (int)si.nPage;
    switch (request) {
        case SB_LINELEFT:
            position -= 44;
            break;
        case SB_LINERIGHT:
            position += 44;
            break;
        case SB_PAGELEFT:
            if (page > 0) {
                position -= page;
            } else {
                position -= 44;
            }
            break;
        case SB_PAGERIGHT:
            if (page > 0) {
                position += page;
            } else {
                position += 44;
            }
            break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
            position = si.nTrackPos;
            break;
        case SB_TOP:
            position = si.nMin;
            break;
        case SB_BOTTOM:
            position = si.nMax;
            break;
        default:
            return;
    }

    int max_position = si.nMax - (int)si.nPage + 1;
    if (max_position < si.nMin) max_position = si.nMin;
    if (position < si.nMin) position = si.nMin;
    if (position > max_position) position = max_position;
    if (position == si.nPos) return;

    si.fMask = SIF_POS;
    si.nPos = position;
    SetScrollInfo(hwnd, bar, &si, TRUE);
    if (bar == SB_HORZ) g_app.bst_tree_scroll_x = position;
    else g_app.bst_tree_scroll_y = position;
    InvalidateRect(hwnd, NULL, FALSE);
}

static void update_bst_search_status(void) {
    char status[192];
    if (g_app.bst_search_path_count == 0) {
        snprintf(status, sizeof(status), "No active zones yet. Start the live feed to build the BST.");
    } else if (g_app.bst_search_step < g_app.bst_search_path_count) {
        const BSTNode *node = g_app.bst_search_path[g_app.bst_search_step];
        if (node->region_id == g_app.bst_search_target) {
            snprintf(status, sizeof(status), "Step %d/%d: target Zone %d matches this node.",
                     g_app.bst_search_step + 1, g_app.bst_search_path_count, g_app.bst_search_target);
        } else {
            const char *comparison = "greater than";
            const char *direction = "RIGHT";
            if (g_app.bst_search_target < node->region_id) {
                comparison = "less than";
                direction = "LEFT";
            }
            snprintf(status, sizeof(status), "Step %d/%d: target Zone %d is %s Zone %d; go %s.",
                     g_app.bst_search_step + 1, g_app.bst_search_path_count,
                     g_app.bst_search_target,
                     comparison, node->region_id, direction);
        }
    } else if (g_app.bst_search_result) {
        const char *plural = "";
        if (g_app.bst_search_path_count != 1) {
            plural = "s";
        }
        snprintf(status, sizeof(status), "Search complete: Zone %d found in %d comparison%s.",
                 g_app.bst_search_target, g_app.bst_search_path_count,
                 plural);
    } else {
        const char *plural = "";
        if (g_app.bst_search_path_count != 1) {
            plural = "s";
        }
        snprintf(status, sizeof(status), "Search complete: Zone %d was not found after %d comparison%s.",
                 g_app.bst_search_target, g_app.bst_search_path_count,
                 plural);
    }
    SetWindowTextA(g_app.hwnd_lbl_bst_step, status);
}

static void set_archive_details(const char *text) {
    SetWindowTextA(g_app.hwnd_edit_search_info, text);
    SendMessage(g_app.hwnd_edit_search_info, EM_SETSEL, 0, 0);
    SendMessage(g_app.hwnd_edit_search_info, EM_SCROLLCARET, 0, 0);
}

static void set_bst_zoom(int zoom_percent) {
    if (zoom_percent < 50) zoom_percent = 50;
    if (zoom_percent > 200) zoom_percent = 200;
    g_app.bst_tree_zoom_percent = zoom_percent;

    BSTLayoutEntry entries[30];
    int count = 0;
    collect_bst_layout(g_app.bst_root, 0, entries, &count);
    update_bst_scrollbars(g_app.hwnd_bst_tree, entries, count);
    InvalidateRect(g_app.hwnd_bst_tree, NULL, FALSE);
}

static void zoom_bst_with_wheel(WPARAM wParam) {
    short wheel_delta = (short)HIWORD(wParam);
    if (wheel_delta > 0) {
        set_bst_zoom(g_app.bst_tree_zoom_percent + 25);
    } else if (wheel_delta < 0) {
        set_bst_zoom(g_app.bst_tree_zoom_percent - 25);
    }
}

static BOOL mouse_pointer_over_bst_tree(LPARAM lParam) {
    POINT point;
    RECT tree_rect;

    point.x = (short)LOWORD(lParam);
    point.y = (short)HIWORD(lParam);
    if (!GetWindowRect(g_app.hwnd_bst_tree, &tree_rect)) {
        return FALSE;
    }
    return PtInRect(&tree_rect, point);
}

static void start_bst_search(int target_region_id) {
    KillTimer(g_app.hwnd_main, 1002);
    g_app.bst_search_target = target_region_id;
    g_app.bst_search_result = NULL;
    g_app.bst_search_step = 0;
    g_app.bst_search_result = bst_search_trace(g_app.bst_root, target_region_id,
                                                g_app.bst_search_path,
                                                &g_app.bst_search_path_count);

    update_bst_search_status();
    InvalidateRect(g_app.hwnd_bst_tree, NULL, FALSE);
    if (g_app.bst_search_path_count > 0 && !SetTimer(g_app.hwnd_main, 1002, 650, NULL)) {
        SetWindowTextA(g_app.hwnd_lbl_bst_step, "Unable to animate the BST search because its timer could not be started.");
        MessageBoxA(g_app.hwnd_main, "The BST search could not be animated because the timer could not be started.",
                    "BST Animation Error", MB_OK | MB_ICONERROR);
    }
}

static LRESULT CALLBACK BSTTreeWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_SIZE) {
        BSTLayoutEntry entries[30];
        int count = 0;
        collect_bst_layout(g_app.bst_root, 0, entries, &count);
        update_bst_scrollbars(hwnd, entries, count);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    if (msg == WM_HSCROLL) {
        scroll_bst_tree(hwnd, SB_HORZ, LOWORD(wParam));
        return 0;
    }
    if (msg == WM_VSCROLL) {
        scroll_bst_tree(hwnd, SB_VERT, LOWORD(wParam));
        return 0;
    }
    if (msg == WM_MOUSEWHEEL) {
        if (mouse_pointer_over_bst_tree(lParam)) {
            zoom_bst_with_wheel(wParam);
            return 0;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    if (msg != WM_PAINT) return DefWindowProc(hwnd, msg, wParam, lParam);

    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT bounds;
    GetClientRect(hwnd, &bounds);
    FillRect(hdc, &bounds, g_app.brush_card);

    HPEN border_pen = CreatePen(PS_SOLID, 1, RGB(226, 232, 240));
    HGDIOBJ old_pen = SelectObject(hdc, border_pen);
    HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, bounds.left, bounds.top, bounds.right, bounds.bottom, 12, 12);
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    DeleteObject(border_pen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(100, 116, 139));
    SelectObject(hdc, g_app.font_sub);
    const char *tree_title = "BINARY SEARCH TREE  |  REGION ID ORDER";
    TextOutA(hdc, 16, 10, tree_title, (int)strlen(tree_title));

    const COLORREF legend_colors[] = {
        RGB(219, 234, 254), RGB(37, 99, 235), RGB(254, 243, 199), RGB(209, 250, 229)
    };
    const char *legend_labels[] = { "Node", "Visited", "Current", "Found" };
    int legend_x = bounds.right - 335;
    for (int i = 0; i < 4; i++) {
        RECT swatch = { legend_x, 12, legend_x + 12, 24 };
        HBRUSH swatch_brush = CreateSolidBrush(legend_colors[i]);
        FillRect(hdc, &swatch, swatch_brush);
        DeleteObject(swatch_brush);
        SetTextColor(hdc, RGB(100, 116, 139));
        TextOutA(hdc, legend_x + 17, 10, legend_labels[i], (int)strlen(legend_labels[i]));
        if (i == 0) {
            legend_x += 64;
        } else {
            legend_x += 78;
        }
    }

    BSTLayoutEntry entries[30];
    int count = 0;
    collect_bst_layout(g_app.bst_root, 0, entries, &count);
    update_bst_scrollbars(hwnd, entries, count);
    if (count == 0) {
        SetTextColor(hdc, RGB(100, 116, 139));
        SelectObject(hdc, g_app.font_body);
        const char *empty_text = "Start the live feed to populate the zone index.";
        RECT text_rect = { 16, 45, bounds.right - 16, bounds.bottom - 12 };
        DrawTextA(hdc, empty_text, -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hwnd, &ps);
        return 0;
    }

    int max_depth = 0;
    for (int i = 0; i < count; i++) {
        if (entries[i].depth > max_depth) max_depth = entries[i].depth;
    }
    int content_width = scale_bst_value(80 + (count - 1) * 88);
    if (content_width < bounds.right) content_width = bounds.right;
    int left_padding = (content_width - scale_bst_value((count - 1) * 88) -
                        scale_bst_value(80)) / 2 + scale_bst_value(40);
    int node_radius = scale_bst_value(14);
    if (node_radius < 7) node_radius = 7;
    for (int i = 0; i < count; i++) {
        entries[i].x = left_padding + scale_bst_value(i * 88) - g_app.bst_tree_scroll_x;
        entries[i].y = scale_bst_value(48 + entries[i].depth * 82) - g_app.bst_tree_scroll_y;
    }

    RECT diagram_clip = { 0, 38, bounds.right, bounds.bottom };
    IntersectClipRect(hdc, diagram_clip.left, diagram_clip.top, diagram_clip.right, diagram_clip.bottom);
    HPEN edge_pen = CreatePen(PS_SOLID, 1, RGB(203, 213, 225));
    old_pen = SelectObject(hdc, edge_pen);
    for (int i = 0; i < count; i++) {
        const BSTNode *children[] = { entries[i].node->left, entries[i].node->right };
        for (int child_index = 0; child_index < 2; child_index++) {
            if (!children[child_index]) continue;
            int child_pos = find_bst_layout_entry(entries, count, children[child_index]);
            if (child_pos < 0) continue;
            if (bst_search_edge_visited(entries[i].node, children[child_index])) {
                HPEN active_pen = CreatePen(PS_SOLID, 2, RGB(37, 99, 235));
                SelectObject(hdc, active_pen);
                MoveToEx(hdc, entries[i].x, entries[i].y, NULL);
                LineTo(hdc, entries[child_pos].x, entries[child_pos].y);
                SelectObject(hdc, edge_pen);
                DeleteObject(active_pen);
            } else {
                MoveToEx(hdc, entries[i].x, entries[i].y, NULL);
                LineTo(hdc, entries[child_pos].x, entries[child_pos].y);
            }
        }
    }
    SelectObject(hdc, old_pen);
    DeleteObject(edge_pen);

    for (int i = 0; i < count; i++) {
        int path_step = find_bst_search_step(entries[i].node);
        COLORREF fill = RGB(219, 234, 254);
        COLORREF outline = RGB(37, 99, 235);
        COLORREF text = RGB(15, 23, 42);
        if (path_step >= 0 && path_step < g_app.bst_search_step) {
            fill = RGB(37, 99, 235);
            text = RGB(255, 255, 255);
        }
        if (path_step >= 0 && path_step == g_app.bst_search_step &&
            g_app.bst_search_step < g_app.bst_search_path_count) {
            fill = RGB(254, 243, 199);
            outline = RGB(217, 119, 6);
            text = RGB(146, 64, 14);
        } else if (g_app.bst_search_result == entries[i].node &&
                   g_app.bst_search_step >= g_app.bst_search_path_count) {
            fill = RGB(209, 250, 229);
            outline = RGB(16, 185, 129);
            text = RGB(6, 78, 59);
        }

        HBRUSH node_brush = CreateSolidBrush(fill);
        HPEN node_pen = CreatePen(PS_SOLID, 2, outline);
        HGDIOBJ previous_brush = SelectObject(hdc, node_brush);
        HGDIOBJ previous_pen = SelectObject(hdc, node_pen);
        Ellipse(hdc, entries[i].x - node_radius, entries[i].y - node_radius,
                entries[i].x + node_radius, entries[i].y + node_radius);
        SelectObject(hdc, previous_brush);
        SelectObject(hdc, previous_pen);
        DeleteObject(node_brush);
        DeleteObject(node_pen);

        char node_label[8];
        snprintf(node_label, sizeof(node_label), "%d", entries[i].node->region_id);
        HFONT node_font = CreateFontA(scale_bst_value(15), 0, 0, 0, FW_SEMIBOLD,
                                      FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
        HGDIOBJ previous_font;
        if (node_font) {
            previous_font = SelectObject(hdc, node_font);
        } else {
            previous_font = SelectObject(hdc, g_app.font_bold);
        }
        SIZE label_size;
        GetTextExtentPoint32A(hdc, node_label, (int)strlen(node_label), &label_size);
        SetTextColor(hdc, text);
        TextOutA(hdc, entries[i].x - label_size.cx / 2, entries[i].y - label_size.cy / 2,
                 node_label, (int)strlen(node_label));
        SelectObject(hdc, previous_font);
        if (node_font) DeleteObject(node_font);
    }

    EndPaint(hwnd, &ps);
    return 0;
}

static HWND create_kpi_card(HWND parent, int x, int y, int w, int h, const char* title, HWND *out_val_hwnd, COLORREF accent_col) {
    HWND panel = CreateWindow("AQIPanelClass", "", WS_CHILD | WS_VISIBLE, x, y, w, h, parent, NULL, NULL, NULL);

    HWND lbl_title = CreateWindow("STATIC", title, WS_CHILD | WS_VISIBLE, 15, 12, w - 30, 18, panel, NULL, NULL, NULL);
    SendMessage(lbl_title, WM_SETFONT, (WPARAM)g_app.font_sub, TRUE);
    SetWindowLongPtr(lbl_title, GWLP_USERDATA, (LONG_PTR)RGB(100, 116, 139));
    apply_unified_theme(lbl_title, g_app.font_sub);

    *out_val_hwnd = CreateWindow("STATIC", "0", WS_CHILD | WS_VISIBLE, 15, 32, w - 30, 32, panel, NULL, NULL, NULL);
    SendMessage(*out_val_hwnd, WM_SETFONT, (WPARAM)g_app.font_kpi_digit, TRUE);
    SetWindowLongPtr(*out_val_hwnd, GWLP_USERDATA, (LONG_PTR)accent_col);
    apply_unified_theme(*out_val_hwnd, g_app.font_kpi_digit);
    apply_unified_theme(panel, g_app.font_sub);

    return panel;
}

static void build_dashboard_page(HWND parent) {
    g_app.hwnd_panel_dash = CreateWindow("AQIPanelClass", "", WS_CHILD | WS_VISIBLE, 15, 110, 1135, 540, parent, NULL, NULL, NULL);

    create_kpi_card(g_app.hwnd_panel_dash, 0, 0, 210, 75, "TOTAL TELEMETRY READINGS", &g_app.hwnd_lbl_kpi_total, RGB(37, 99, 235));
    create_kpi_card(g_app.hwnd_panel_dash, 225, 0, 210, 75, "CRITICAL ALERTS IN HEAP", &g_app.hwnd_lbl_kpi_alerts, RGB(225, 29, 72));
    create_kpi_card(g_app.hwnd_panel_dash, 450, 0, 210, 75, "ACTIVE MUMBAI ZONES", &g_app.hwnd_lbl_kpi_regions, RGB(16, 185, 129));
    create_kpi_card(g_app.hwnd_panel_dash, 675, 0, 210, 75, "NETWORK AVERAGE AQI", &g_app.hwnd_lbl_kpi_avg, RGB(217, 119, 6));
    create_kpi_card(g_app.hwnd_panel_dash, 900, 0, 225, 75, "PEAK AQI RECORDED", &g_app.hwnd_lbl_kpi_peak, RGB(147, 51, 234));

    HWND lbl_spark = CreateWindow("STATIC", "Real-Time Telemetry Stream (Threshold Bar > 300 AQI)", WS_CHILD | WS_VISIBLE, 0, 92, 500, 20, g_app.hwnd_panel_dash, NULL, NULL, NULL);
    SendMessage(lbl_spark, WM_SETFONT, (WPARAM)g_app.font_bold, TRUE);

    g_app.hwnd_sparkline = CreateWindow("SparklineClass", "", WS_CHILD | WS_VISIBLE, 0, 115, 1125, 140, g_app.hwnd_panel_dash, NULL, NULL, NULL);

    HWND lbl_recent = CreateWindow("STATIC", "Live Sensor Telemetry Feed", WS_CHILD | WS_VISIBLE, 0, 270, 300, 20, g_app.hwnd_panel_dash, NULL, NULL, NULL);
    SendMessage(lbl_recent, WM_SETFONT, (WPARAM)g_app.font_bold, TRUE);

    g_app.hwnd_lv_recent = CreateWindow(WC_LISTVIEW, "", WS_CHILD | WS_VISIBLE | LVS_REPORT | WS_BORDER, 0, 295, 1125, 235, g_app.hwnd_panel_dash, NULL, NULL, NULL);
    SendMessage(g_app.hwnd_lv_recent, WM_SETFONT, (WPARAM)g_app.font_body, TRUE);
    style_listview_modern(g_app.hwnd_lv_recent);

    const char* cols[] = { "Ingest Time", "Mumbai Zone Location", "PM2.5 (ug/m3)", "AQI Index", "Severity Level" };
    create_listview_columns(g_app.hwnd_lv_recent, cols, 5);
}

static void build_alerts_page(HWND parent) {
    g_app.hwnd_panel_alerts = CreateWindow("AQIPanelClass", "", WS_CHILD, 15, 110, 1135, 540, parent, NULL, NULL, NULL);

    HWND btn_ext = CreateWindow("BUTTON", "Acknowledge Highest (Extract-Max)", WS_CHILD | WS_VISIBLE | BS_FLAT, 0, 0, 260, 32, g_app.hwnd_panel_alerts, (HMENU)2004, NULL, NULL);
    SendMessage(btn_ext, WM_SETFONT, (WPARAM)g_app.font_bold, TRUE);
    apply_unified_theme(btn_ext, g_app.font_bold);

    g_app.hwnd_lv_alerts = CreateWindow(WC_LISTVIEW, "", WS_CHILD | WS_VISIBLE | LVS_REPORT | WS_BORDER, 0, 45, 1125, 485, g_app.hwnd_panel_alerts, NULL, NULL, NULL);
    SendMessage(g_app.hwnd_lv_alerts, WM_SETFONT, (WPARAM)g_app.font_body, TRUE);
    style_listview_modern(g_app.hwnd_lv_alerts);

    const char* cols[] = { "Alert Time", "Mumbai Region Name", "AQI Severity", "PM2.5 Level", "Alert Level" };
    create_listview_columns(g_app.hwnd_lv_alerts, cols, 5);
}

static void build_archive_page(HWND parent) {
    g_app.hwnd_panel_archive = CreateWindow("AQIPanelClass", "", WS_CHILD, 15, 110, 1135, 540, parent, NULL, NULL, NULL);

    HWND lbl_sel = CreateWindow("STATIC", "Select Mumbai Zone:", WS_CHILD | WS_VISIBLE, 0, 12, 140, 22, g_app.hwnd_panel_archive, NULL, NULL, NULL);
    SendMessage(lbl_sel, WM_SETFONT, (WPARAM)g_app.font_bold, TRUE);
    apply_unified_theme(lbl_sel, g_app.font_bold);

    g_app.hwnd_combo_search = CreateWindow("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 145, 8, 300, 300, g_app.hwnd_panel_archive, (HMENU)2008, NULL, NULL);
    SendMessage(g_app.hwnd_combo_search, WM_SETFONT, (WPARAM)g_app.font_body, TRUE);
    apply_unified_theme(g_app.hwnd_combo_search, g_app.font_body);

    HWND btn_sch = CreateWindow("BUTTON", "Search Index (BST)", WS_CHILD | WS_VISIBLE | BS_FLAT, 455, 7, 160, 30, g_app.hwnd_panel_archive, (HMENU)2005, NULL, NULL);
    SendMessage(btn_sch, WM_SETFONT, (WPARAM)g_app.font_bold, TRUE);
    apply_unified_theme(btn_sch, g_app.font_bold);

    g_app.hwnd_lbl_bst_step = CreateWindow("STATIC", "Select a zone and search to watch the BST compare nodes.", WS_CHILD | WS_VISIBLE, 635, 12, 480, 22, g_app.hwnd_panel_archive, NULL, NULL, NULL);
    SendMessage(g_app.hwnd_lbl_bst_step, WM_SETFONT, (WPARAM)g_app.font_sub, TRUE);
    apply_unified_theme(g_app.hwnd_lbl_bst_step, g_app.font_sub);

    g_app.hwnd_lbl_search_details = CreateWindow("STATIC", "SELECTED LOCATION DETAILS", WS_CHILD | WS_VISIBLE, 800, 50, 320, 20, g_app.hwnd_panel_archive, NULL, NULL, NULL);
    SendMessage(g_app.hwnd_lbl_search_details, WM_SETFONT, (WPARAM)g_app.font_bold, TRUE);
    apply_unified_theme(g_app.hwnd_lbl_search_details, g_app.font_bold);

    g_app.hwnd_bst_tree = CreateWindow("BSTTreeClass", "", WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL | WS_BORDER, 0, 76, 780, 450, g_app.hwnd_panel_archive, NULL, NULL, NULL);

    g_app.hwnd_edit_search_info = CreateWindow("EDIT", "Select an active Mumbai region from the dropdown menu to view its current and historical metrics.",
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        800, 76, 320, 450, g_app.hwnd_panel_archive, NULL, NULL, NULL);
    SendMessage(g_app.hwnd_edit_search_info, WM_SETFONT, (WPARAM)g_app.font_body, TRUE);
    SendMessage(g_app.hwnd_edit_search_info, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(10, 8));
    apply_unified_theme(g_app.hwnd_edit_search_info, g_app.font_body);
}

static void collect_report_row(const BSTNode *node, HWND lv) {
    const char *rname = sensor_region_name(node->region_id);

    char r_id[16], c_aqi[16], avg_aqi[32], peak[16], count[32], time_buf[16];
    format_time_label(node->timestamp, time_buf, sizeof(time_buf));
    double avg = 0.0;
    if (node->reading_count > 0) {
        avg = (double)node->sum_aqi / (double)node->reading_count;
    }

    snprintf(r_id, sizeof(r_id), "%d", node->region_id);
    snprintf(c_aqi, sizeof(c_aqi), "%d", node->aqi_val);
    snprintf(avg_aqi, sizeof(avg_aqi), "%.1f", avg);
    snprintf(peak, sizeof(peak), "%d", node->peak_aqi);
    snprintf(count, sizeof(count), "%ld", node->reading_count);

    LVITEM lvi = { 0 };
    lvi.mask = LVIF_TEXT;
    lvi.iItem = ListView_GetItemCount(lv);
    lvi.pszText = r_id;
    int idx = ListView_InsertItem(lv, &lvi);

    ListView_SetItemText(lv, idx, 1, (LPSTR)rname);
    ListView_SetItemText(lv, idx, 2, c_aqi);
    ListView_SetItemText(lv, idx, 3, (LPSTR)sensor_aqi_category(node->aqi_val));
    ListView_SetItemText(lv, idx, 4, avg_aqi);
    ListView_SetItemText(lv, idx, 5, peak);
    ListView_SetItemText(lv, idx, 6, count);
    ListView_SetItemText(lv, idx, 7, time_buf);
}

static void build_reports_page(HWND parent) {
    g_app.hwnd_panel_reports = CreateWindow("AQIPanelClass", "", WS_CHILD, 15, 110, 1135, 540, parent, NULL, NULL, NULL);

    HWND btn_gen = CreateWindow("BUTTON", "Generate Ordered Report (In-Order BST)", WS_CHILD | WS_VISIBLE | BS_FLAT, 0, 0, 290, 32, g_app.hwnd_panel_reports, (HMENU)2006, NULL, NULL);
    SendMessage(btn_gen, WM_SETFONT, (WPARAM)g_app.font_bold, TRUE);
    apply_unified_theme(btn_gen, g_app.font_bold);

    g_app.hwnd_lbl_report_stats = CreateWindow("STATIC", "Generate a report to view all active zones.", WS_CHILD | WS_VISIBLE, 310, 8, 790, 20, g_app.hwnd_panel_reports, NULL, NULL, NULL);
    SendMessage(g_app.hwnd_lbl_report_stats, WM_SETFONT, (WPARAM)g_app.font_body, TRUE);
    apply_unified_theme(g_app.hwnd_lbl_report_stats, g_app.font_body);

    g_app.hwnd_lv_report = CreateWindow(WC_LISTVIEW, "", WS_CHILD | WS_VISIBLE | LVS_REPORT | WS_BORDER, 0, 45, 1125, 485, g_app.hwnd_panel_reports, NULL, NULL, NULL);
    SendMessage(g_app.hwnd_lv_report, WM_SETFONT, (WPARAM)g_app.font_body, TRUE);
    style_listview_modern(g_app.hwnd_lv_report);

    const char* cols[] = { "ID", "Mumbai Zone Name", "Current AQI", "Severity", "Avg AQI", "Peak AQI", "Total Logs", "Last Updated" };
    create_listview_columns(g_app.hwnd_lv_report, cols, 8);
}

static void generate_report(void) {
    ListView_DeleteAllItems(g_app.hwnd_lv_report);

    const BSTNode *nodes[30];
    int node_count = 0;
    bst_inorder(g_app.bst_root, nodes, &node_count);
    for (int i = 0; i < node_count; i++) {
        collect_report_row(nodes[i], g_app.hwnd_lv_report);
    }

    char stats[192];
    snprintf(stats, sizeof(stats), "In-Order BST Traversal Execution Complete | Active Zones: %d | Tree Depth: %d | Rows Shown: %d",
             bst_count(g_app.bst_root), bst_height(g_app.bst_root), ListView_GetItemCount(g_app.hwnd_lv_report));
    SetWindowText(g_app.hwnd_lbl_report_stats, stats);
}

static void show_tab_panel(int index) {
    if (index == 0) {
        ShowWindow(g_app.hwnd_panel_dash, SW_SHOW);
    } else {
        ShowWindow(g_app.hwnd_panel_dash, SW_HIDE);
    }
    if (index == 1) {
        ShowWindow(g_app.hwnd_panel_alerts, SW_SHOW);
    } else {
        ShowWindow(g_app.hwnd_panel_alerts, SW_HIDE);
    }
    if (index == 2) {
        ShowWindow(g_app.hwnd_panel_archive, SW_SHOW);
    } else {
        ShowWindow(g_app.hwnd_panel_archive, SW_HIDE);
    }
    if (index == 3) {
        ShowWindow(g_app.hwnd_panel_reports, SW_SHOW);
    } else {
        ShowWindow(g_app.hwnd_panel_reports, SW_HIDE);
    }
}

static void resize_dashboard_children(int panel_w, int panel_h) {
    if (!g_app.hwnd_sparkline) return;
    MoveWindow(g_app.hwnd_sparkline, 0, 115, panel_w, 140, TRUE);

    int lv_h = panel_h - 295 - 5;
    if (lv_h < 80) lv_h = 80;
    MoveWindow(g_app.hwnd_lv_recent, 0, 295, panel_w, lv_h, TRUE);
}

static void resize_alerts_children(int panel_w, int panel_h) {
    if (!g_app.hwnd_lv_alerts) return;
    int lv_h = panel_h - 45 - 5;
    if (lv_h < 80) lv_h = 80;
    MoveWindow(g_app.hwnd_lv_alerts, 0, 45, panel_w, lv_h, TRUE);
}

static void resize_archive_children(int panel_w, int panel_h) {
    if (!g_app.hwnd_bst_tree || !g_app.hwnd_edit_search_info) return;
    const int COLUMN_GAP = 14;
    const int CONTENT_TOP = 76;
    int left_w = (panel_w - COLUMN_GAP) * 7 / 10;
    int right_x = left_w + COLUMN_GAP;
    int right_w = panel_w - right_x;
    int content_h = panel_h - CONTENT_TOP - 5;
    if (content_h < 100) content_h = 100;

    MoveWindow(g_app.hwnd_lbl_bst_step, 182, 48, left_w - 182, 24, TRUE);
    MoveWindow(g_app.hwnd_bst_tree, 0, CONTENT_TOP, left_w, content_h, TRUE);
    MoveWindow(g_app.hwnd_lbl_search_details, right_x, 48, right_w, 24, TRUE);
    MoveWindow(g_app.hwnd_edit_search_info, right_x, CONTENT_TOP, right_w, content_h, TRUE);
}

static void resize_reports_children(int panel_w, int panel_h) {
    if (!g_app.hwnd_lv_report) return;
    int lv_h = panel_h - 45 - 5;
    if (lv_h < 80) lv_h = 80;
    MoveWindow(g_app.hwnd_lv_report, 0, 45, panel_w, lv_h, TRUE);
}

static void layout_ui(int client_w, int client_h) {
    if (!g_app.hwnd_header || client_w <= 0 || client_h <= 0) return;

    const int MARGIN = 15;
    const int TAB_X = 290;
    const int TAB_Y = 60;
    const int TAB_H = 36;
    const int PANEL_Y = 110;
    const int STATUS_H = 26;

    MoveWindow(g_app.hwnd_header, 0, 0, client_w, 50, TRUE);

    int tab_w = client_w - TAB_X - MARGIN;
    if (tab_w < 100) tab_w = 100;
    MoveWindow(g_app.hwnd_tab, TAB_X, TAB_Y, tab_w, TAB_H, TRUE);

    int panel_w = client_w - 2 * MARGIN;
    int panel_h = client_h - PANEL_Y - STATUS_H - MARGIN;
    if (panel_w < 400) panel_w = 400;
    if (panel_h < 200) panel_h = 200;

    MoveWindow(g_app.hwnd_panel_dash, MARGIN, PANEL_Y, panel_w, panel_h, TRUE);
    MoveWindow(g_app.hwnd_panel_alerts, MARGIN, PANEL_Y, panel_w, panel_h, TRUE);
    MoveWindow(g_app.hwnd_panel_archive, MARGIN, PANEL_Y, panel_w, panel_h, TRUE);
    MoveWindow(g_app.hwnd_panel_reports, MARGIN, PANEL_Y, panel_w, panel_h, TRUE);

    resize_dashboard_children(panel_w, panel_h);
    resize_alerts_children(panel_w, panel_h);
    resize_archive_children(panel_w, panel_h);
    resize_reports_children(panel_w, panel_h);
}

static void enable_dpi_awareness(void) {
    SetProcessDPIAware();
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_app.font_title = CreateFont(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
            g_app.font_sub = CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
            g_app.font_body = CreateFont(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
            g_app.font_bold = CreateFont(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
            g_app.font_kpi_digit = CreateFont(26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");

            g_app.brush_bg = CreateSolidBrush(RGB(248, 250, 252));
            g_app.brush_card = CreateSolidBrush(RGB(255, 255, 255));
            g_app.brush_header = CreateSolidBrush(RGB(15, 23, 42));

            g_app.hwnd_header = CreateWindow("HeaderClass", "", WS_CHILD | WS_VISIBLE, 0, 0, 1180, 50, hwnd, NULL, NULL, NULL);

            g_app.hwnd_btn_start = CreateWindow("BUTTON", "Start Live Feed", WS_CHILD | WS_VISIBLE | BS_FLAT, 15, 60, 130, 32, hwnd, (HMENU)2002, NULL, NULL);
            SendMessage(g_app.hwnd_btn_start, WM_SETFONT, (WPARAM)g_app.font_bold, TRUE);
            apply_unified_theme(g_app.hwnd_btn_start, g_app.font_bold);

            g_app.hwnd_btn_stop = CreateWindow("BUTTON", "Pause Feed", WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_FLAT, 155, 60, 120, 32, hwnd, (HMENU)2003, NULL, NULL);
            SendMessage(g_app.hwnd_btn_stop, WM_SETFONT, (WPARAM)g_app.font_bold, TRUE);
            apply_unified_theme(g_app.hwnd_btn_stop, g_app.font_bold);

            g_app.hwnd_tab = CreateWindow(WC_TABCONTROL, "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 290, 60, 860, 36, hwnd, (HMENU)2001, NULL, NULL);
            SendMessage(g_app.hwnd_tab, WM_SETFONT, (WPARAM)g_app.font_bold, TRUE);
            apply_unified_theme(g_app.hwnd_tab, g_app.font_bold);
            TabCtrl_SetCurSel(g_app.hwnd_tab, 0);

            TCITEM tie = { 0 };
            tie.mask = TCIF_TEXT;
            tie.pszText = "  Overview Dashboard  ";
            TabCtrl_InsertItem(g_app.hwnd_tab, 0, &tie);
            tie.pszText = "  Emergency Queue  ";
            TabCtrl_InsertItem(g_app.hwnd_tab, 1, &tie);
            tie.pszText = "  Zone Archive  ";
            TabCtrl_InsertItem(g_app.hwnd_tab, 2, &tie);
            tie.pszText = "  Reports Engine  ";
            TabCtrl_InsertItem(g_app.hwnd_tab, 3, &tie);

            build_dashboard_page(hwnd);
            build_alerts_page(hwnd);
            build_archive_page(hwnd);
            build_reports_page(hwnd);

            show_tab_panel(0);

            g_app.hwnd_status = CreateWindow(STATUSCLASSNAME, "", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, NULL, NULL);
            SendMessage(g_app.hwnd_status, WM_SETFONT, (WPARAM)g_app.font_sub, TRUE);
            apply_unified_theme(g_app.hwnd_status, g_app.font_sub);
            update_kpi_labels(&g_app);

            apply_unified_theme(g_app.hwnd_lv_recent, g_app.font_body);
            apply_unified_theme(g_app.hwnd_lv_alerts, g_app.font_body);
            apply_unified_theme(g_app.hwnd_lv_report, g_app.font_body);

            RECT client_rc;
            GetClientRect(hwnd, &client_rc);
            layout_ui(client_rc.right - client_rc.left, client_rc.bottom - client_rc.top);
            break;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORLISTBOX: {
            HDC hdcStatic = (HDC)wParam;
            COLORREF accent = (COLORREF)GetWindowLongPtr((HWND)lParam, GWLP_USERDATA);
            if (msg == WM_CTLCOLOREDIT) {
                SetBkMode(hdcStatic, OPAQUE);
                SetBkColor(hdcStatic, RGB(255, 255, 255));
            } else {
                SetBkMode(hdcStatic, TRANSPARENT);
            }
            if (accent) {
                SetTextColor(hdcStatic, accent);
            } else {
                SetTextColor(hdcStatic, RGB(15, 23, 42));
            }
            return (LRESULT)g_app.brush_card;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case 2002:
                    if (!g_app.running) {
                        g_app.running = TRUE;
                        SetTimer(hwnd, 1001, g_app.interval_ms, NULL);
                        EnableWindow(g_app.hwnd_btn_start, FALSE);
                        EnableWindow(g_app.hwnd_btn_stop, TRUE);
                        update_kpi_labels(&g_app);
                    }
                    break;

                case 2003:
                    if (g_app.running) {
                        g_app.running = FALSE;
                        KillTimer(hwnd, 1001);
                        EnableWindow(g_app.hwnd_btn_start, TRUE);
                        EnableWindow(g_app.hwnd_btn_stop, FALSE);
                        update_kpi_labels(&g_app);
                    }
                    break;

                
                case 2004:
                    if (!heap_is_empty(&g_app.heap)) {
                        
                        AlertNode top_alert = heap_extract_max(&g_app.heap);

                        
                        char ack_msg[384], time_buf[16];
                        format_time_label(top_alert.timestamp, time_buf, sizeof(time_buf));

                        snprintf(ack_msg, sizeof(ack_msg),
                                 "CRITICAL ALERT ACKNOWLEDGED & DISPATCHED\n"
                                 "--------------------------------------------------\n"
                                 "Zone Location:  %s (ID: %d)\n"
                                 "Highest AQI:    %d (%s)\n"
                                 "PM2.5 Density:  %.1f ug/m3\n"
                                 "Ingest Time:    %s\n\n"
                                 "The item was removed from priority queue.",
                                 sensor_region_name(top_alert.region_id),
                                 top_alert.region_id,
                                 top_alert.aqi,
                                 sensor_aqi_category(top_alert.aqi),
                                 top_alert.pm25,
                                 time_buf);

                        MessageBox(hwnd, ack_msg, "Priority Emergency Acknowledged", MB_OK | MB_ICONINFORMATION);

                        
                        refresh_alerts_listview(&g_app);
                        update_kpi_labels(&g_app);
                    } else {
                        MessageBox(hwnd, "No critical AQI emergency alerts currently pending in heap queue.",
                                   "Queue Empty", MB_OK | MB_ICONEXCLAMATION);
                    }
                    break;

                case 2005: {
                    int sel = (int)SendMessage(g_app.hwnd_combo_search, CB_GETCURSEL, 0, 0);
                    if (sel == CB_ERR) {
                        KillTimer(hwnd, 1002);
                        g_app.bst_search_path_count = 0;
                        g_app.bst_search_step = 0;
                        g_app.bst_search_result = NULL;
                        update_bst_search_status();
                        InvalidateRect(g_app.hwnd_bst_tree, NULL, FALSE);
                        set_archive_details("No telemetry records ingested yet.");
                        break;
                    }
                    int rid = (int)SendMessage(g_app.hwnd_combo_search, CB_GETITEMDATA, sel, 0);
                    start_bst_search(rid);
                    const BSTNode *found = g_app.bst_search_result;

                    if (found) {
                        char info[512], time_buf[16];
                        format_time_label(found->timestamp, time_buf, sizeof(time_buf));
                        double avg = 0.0;
                        if (found->reading_count > 0) {
                            avg = (double)found->sum_aqi / (double)found->reading_count;
                        }

                        snprintf(info, sizeof(info),
                                 "LOCATION\r\n%s\r\nZone ID: %d\r\n\r\n"
                                 "CURRENT AIR QUALITY\r\nAQI: %d (%s)\r\nPM2.5: %.1f ug/m3\r\n\r\n"
                                 "HISTORY\r\nReadings: %ld\r\nAverage AQI: %.1f\r\nPeak AQI: %d\r\nLast updated: %s",
                                 sensor_region_name(found->region_id), found->region_id, found->aqi_val,
                                 sensor_aqi_category(found->aqi_val), found->pm25_val, found->reading_count,
                                 avg, found->peak_aqi, time_buf);
                        set_archive_details(info);
                    } else {
                        char info[192];
                        snprintf(info, sizeof(info), "Zone %d is not present in the current BST.", rid);
                        set_archive_details(info);
                    }
                    break;
                }

                case 2006:
                    generate_report();
                    break;

            }
            break;
        }

        case WM_NOTIFY: {
            LPNMHDR nm = (LPNMHDR)lParam;
            if (nm->idFrom == 2001 && nm->code == TCN_SELCHANGE) {
                int cur = TabCtrl_GetCurSel(g_app.hwnd_tab);
                show_tab_panel(cur);
            }

            if (nm->code == LVN_COLUMNCLICK) {
                NMLISTVIEW *pnmv = (NMLISTVIEW *)lParam;
                handle_column_click(pnmv->hdr.hwndFrom, pnmv->iSubItem);
            }

            if (nm->code == NM_CUSTOMDRAW) {
                LPNMLVCUSTOMDRAW pLVCD = (LPNMLVCUSTOMDRAW)lParam;
                if (pLVCD->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;

                if (pLVCD->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    char aqi_str[32] = {0};
                    int aqi_col = 3;
                    if (pLVCD->nmcd.hdr.hwndFrom == g_app.hwnd_lv_report ||
                        pLVCD->nmcd.hdr.hwndFrom == g_app.hwnd_lv_alerts) {
                        aqi_col = 2;
                    }
                    ListView_GetItemText(pLVCD->nmcd.hdr.hwndFrom, pLVCD->nmcd.dwItemSpec, aqi_col, aqi_str, sizeof(aqi_str));
                    int aqi = atoi(aqi_str);

                    if (aqi > 300) {
                        pLVCD->clrTextBk = RGB(254, 226, 226);
                        pLVCD->clrText = RGB(159, 18, 57);
                    } else if (aqi > 100) {
                        pLVCD->clrTextBk = RGB(254, 243, 199);
                        pLVCD->clrText = RGB(146, 64, 14);
                    } else if (aqi > 0) {
                        pLVCD->clrTextBk = RGB(209, 250, 229);
                        pLVCD->clrText = RGB(6, 78, 59);
                    }
                    return CDRF_DODEFAULT;
                }
            }
            break;
        }

        case WM_TIMER: {
            if (wParam == 1001 && g_app.running) {
                SensorReading reading;
                sensor_generate_reading(&reading);
                ingest_reading(&g_app, reading);
            } else if (wParam == 1002) {
                if (g_app.bst_search_step < g_app.bst_search_path_count) {
                    g_app.bst_search_step++;
                    update_bst_search_status();
                    InvalidateRect(g_app.hwnd_bst_tree, NULL, FALSE);
                }
                if (g_app.bst_search_step >= g_app.bst_search_path_count) {
                    KillTimer(hwnd, 1002);
                }
            }
            break;
        }

        case WM_SIZE: {
            SendMessage(g_app.hwnd_status, WM_SIZE, 0, 0);
            layout_ui(LOWORD(lParam), HIWORD(lParam));
            break;
        }

        case WM_MOUSEWHEEL:
            if (mouse_pointer_over_bst_tree(lParam)) {
                zoom_bst_with_wheel(wParam);
                return 0;
            }
            break;

        case WM_GETMINMAXINFO: {
            MINMAXINFO *mmi = (MINMAXINFO *)lParam;
            mmi->ptMinTrackSize.x = 1170; 
            mmi->ptMinTrackSize.y = 650;
            break;
        }

        case WM_DESTROY: {
            KillTimer(hwnd, 1001);
            KillTimer(hwnd, 1002);
            bst_free(g_app.bst_root);
            heap_free(&g_app.heap);

            for (int i = g_app.toast_count - 1; i >= 0; i--) {
                DestroyWindow(g_app.toast_hwnds[i]);
            }

            DeleteObject(g_app.font_title);
            DeleteObject(g_app.font_sub);
            DeleteObject(g_app.font_body);
            DeleteObject(g_app.font_bold);
            DeleteObject(g_app.font_kpi_digit);

            DeleteObject(g_app.brush_bg);
            DeleteObject(g_app.brush_card);
            DeleteObject(g_app.brush_header);

            PostQuitMessage(0);
            break;
        }

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    srand((unsigned int)time(NULL));
    enable_dpi_awareness();

    INITCOMMONCONTROLSEX icex = { 0 };
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES;
    InitCommonControlsEx(&icex);

    memset(&g_app, 0, sizeof(AppState));
    heap_init(&g_app.heap);
    g_app.interval_ms = 500;
    g_app.bst_tree_zoom_percent = 100;

    WNDCLASS wcSpark = { 0 };
    wcSpark.lpfnWndProc = SparklineWndProc;
    wcSpark.hInstance = hInstance;
    wcSpark.lpszClassName = "SparklineClass";
    RegisterClass(&wcSpark);

    WNDCLASS wcBSTTree = { 0 };
    wcBSTTree.lpfnWndProc = BSTTreeWndProc;
    wcBSTTree.hInstance = hInstance;
    wcBSTTree.lpszClassName = "BSTTreeClass";
    wcBSTTree.hbrBackground = CreateSolidBrush(RGB(255, 255, 255));
    RegisterClass(&wcBSTTree);

    WNDCLASS wcPanel = { 0 };
    wcPanel.lpfnWndProc = AQIPanelWndProc;
    wcPanel.hInstance = hInstance;
    wcPanel.lpszClassName = "AQIPanelClass";
    wcPanel.hbrBackground = CreateSolidBrush(RGB(255, 255, 255));
    RegisterClass(&wcPanel);

    WNDCLASS wcHeader = { 0 };
    wcHeader.lpfnWndProc = HeaderWndProc;
    wcHeader.hInstance = hInstance;
    wcHeader.lpszClassName = "HeaderClass";
    RegisterClass(&wcHeader);

    WNDCLASS wcToast = { 0 };
    wcToast.lpfnWndProc = ToastWndProc;
    wcToast.hInstance = hInstance;
    wcToast.lpszClassName = "ToastClass";
    RegisterClass(&wcToast);

    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = CreateSolidBrush(RGB(248, 250, 252));
    wc.lpszClassName = "AQIWin32EnterpriseEngine";

    RegisterClass(&wc);

    HWND hwnd = CreateWindow("AQIWin32EnterpriseEngine",
                             "AEROVISION | Enterprise Air Quality Intelligence Platform",
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME | WS_VISIBLE,
                             20, 20, 1180, 720, NULL, NULL, hInstance, NULL);

    g_app.hwnd_main = hwnd;

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}