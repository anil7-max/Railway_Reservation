/*
 * RAILWAY RESERVATION SYSTEM - MASTER V4
 * C + GTK3
 *
 * Build:
 *   gcc -Wall -Wextra -Wpedantic railway_reservation_master_v4.c -o railway_master \
 *       $(pkg-config --cflags --libs gtk+-3.0)
 *
 * A polished educational railway reservation dashboard built around
 * FIFO linked-list queues for confirmed and waiting passengers.
 *
 * Highlights:
 *   - Modern GTK3 HeaderBar + Stack + StackSidebar navigation
 *   - Responsive dashboard with live statistics
 *   - Coach/class availability cards and progress bars
 *   - Recent-booking cards and live activity timeline
 *   - Full booking form with email and berth preference
 *   - Search by Ticket ID or PNR
 *   - Class/status filters for passenger management
 *   - Selected-passenger details panel
 *   - Automatic seat allocation and same-class FIFO promotion
 *   - E-ticket dialog with Copy PNR and Save Ticket
 *   - Confirmation dialogs, status badges, notifications
 *   - Settings page with Dark Mode and confirmation preferences
 *   - No third-party GUI library beyond GTK3 / GLib
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <ctype.h>

#define NAME_LEN 48
#define PHONE_LEN 20
#define EMAIL_LEN 80
#define FROM_LEN 32
#define TO_LEN 32
#define CLASS_LEN 8
#define GENDER_LEN 12
#define BERTH_LEN 24
#define PNR_LEN 20
#define DATE_LEN 20
#define SEAT_LEN 16

#define MAX_CONFIRMED 38
#define MAX_WAITING 10

#define SL_CAP 20
#define A3_CAP 10
#define A2_CAP 5
#define A1_CAP 3

typedef struct Passenger {
    int ticket_id;
    char pnr[PNR_LEN];
    char name[NAME_LEN];
    int age;
    char gender[GENDER_LEN];
    char phone[PHONE_LEN];
    char email[EMAIL_LEN];
    char travel_class[CLASS_LEN];
    char from[FROM_LEN];
    char to[TO_LEN];
    char date[DATE_LEN];
    char berth[BERTH_LEN];
    char seat[SEAT_LEN];
    int waiting_position;
    struct Passenger *next;
} Passenger;

typedef struct {
    Passenger *front;
    Passenger *rear;
    int size;
} Queue;

/* ---------- Global application state ---------- */

static Queue confirmed;
static Queue waiting;

static int next_ticket = 1001;
static unsigned long long next_pnr = 4817263951ULL;
static int total_bookings = 0;
static int total_cancelled = 0;
static int total_promoted = 0;

static gboolean dark_mode = FALSE;
static gboolean confirm_actions = TRUE;

static GtkWidget *window;
static GtkWidget *main_stack;
static GtkWidget *header_clock;
static GtkWidget *header_status;

/* Booking widgets */
static GtkWidget *entry_name;
static GtkWidget *spin_age;
static GtkWidget *combo_gender;
static GtkWidget *entry_phone;
static GtkWidget *entry_email;
static GtkWidget *combo_class;
static GtkWidget *combo_berth;
static GtkWidget *entry_date;
static GtkWidget *booking_availability_label;
static GtkWidget *booking_availability_progress;

/* Search/cancellation widgets */
static GtkWidget *entry_search;
static GtkWidget *entry_cancel;
static GtkWidget *combo_filter_class;
static GtkWidget *combo_filter_status;

/* Dashboard widgets */
static GtkWidget *lbl_confirmed;
static GtkWidget *lbl_waiting;
static GtkWidget *lbl_available;
static GtkWidget *lbl_cancelled;
static GtkWidget *dash_total;
static GtkWidget *dash_promoted;
static GtkWidget *class_free[4];
static GtkWidget *class_progress[4];
static GtkListBox *recent_list;
static GtkTextBuffer *dash_activity_buffer;

/* Passenger details */
static int selected_ticket_id = -1;
static GtkWidget *detail_status;
static GtkWidget *detail_name;
static GtkWidget *detail_ticket;
static GtkWidget *detail_pnr;
static GtkWidget *detail_class;
static GtkWidget *detail_seat;
static GtkWidget *detail_date;
static GtkWidget *detail_phone;
static GtkWidget *detail_email;
static GtkWidget *detail_berth;

/* Passenger tables */
static GtkListStore *store_confirmed;
static GtkListStore *store_waiting;

/* Activity */
static GtkWidget *log_view;
static GtkTextBuffer *log_buffer;
static GtkWidget *lbl_status;

/* Settings */
static GtkWidget *switch_dark;
static GtkWidget *switch_confirm;

/* Forward declarations for UI callbacks/helpers used before their definitions. */
static void show_notification(const char *message, GtkMessageType type);
static void show_ticket_dialog(Passenger *p);

/* ---------- Queue functions ---------- */

static void queue_init(Queue *q)
{
    if (!q) return;
    q->front = NULL;
    q->rear = NULL;
    q->size = 0;
}

static void enqueue(Queue *q, Passenger *p)
{
    if (!q || !p) return;

    p->next = NULL;
    if (q->rear)
        q->rear->next = p;
    else
        q->front = p;

    q->rear = p;
    q->size++;
}

static Passenger *dequeue(Queue *q)
{
    Passenger *p;

    if (!q || !q->front)
        return NULL;

    p = q->front;
    q->front = p->next;
    if (!q->front)
        q->rear = NULL;

    p->next = NULL;
    q->size--;
    return p;
}

static Passenger *remove_ticket(Queue *q, int ticket_id)
{
    Passenger *prev = NULL;
    Passenger *cur;

    if (!q) return NULL;

    cur = q->front;
    while (cur) {
        if (cur->ticket_id == ticket_id) {
            if (prev)
                prev->next = cur->next;
            else
                q->front = cur->next;

            if (q->rear == cur)
                q->rear = prev;

            cur->next = NULL;
            q->size--;
            return cur;
        }
        prev = cur;
        cur = cur->next;
    }

    return NULL;
}

static Passenger *remove_first_class(Queue *q, const char *class_code)
{
    Passenger *prev = NULL;
    Passenger *cur;

    if (!q || !class_code) return NULL;

    cur = q->front;
    while (cur) {
        if (strcmp(cur->travel_class, class_code) == 0) {
            if (prev)
                prev->next = cur->next;
            else
                q->front = cur->next;

            if (q->rear == cur)
                q->rear = prev;

            cur->next = NULL;
            q->size--;
            return cur;
        }
        prev = cur;
        cur = cur->next;
    }

    return NULL;
}

static void free_queue(Queue *q)
{
    Passenger *p;

    if (!q) return;
    while ((p = dequeue(q)) != NULL)
        free(p);
}

/* ---------- Utility ---------- */

static void copy_text(char *dest, size_t dest_size, const char *src)
{
    if (!dest || dest_size == 0) return;
    if (!src) {
        dest[0] = '\0';
        return;
    }
    snprintf(dest, dest_size, "%s", src);
}

static void trim_spaces(char *s)
{
    char *start;
    char *end;

    if (!s) return;

    start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
        start++;

    if (start != s)
        memmove(s, start, strlen(start) + 1);

    if (*s == '\0') return;

    end = s + strlen(s) - 1;
    while (end >= s && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        *end = '\0';
        end--;
    }
}

static int class_capacity(const char *code)
{
    if (strcmp(code, "SL") == 0) return SL_CAP;
    if (strcmp(code, "3A") == 0) return A3_CAP;
    if (strcmp(code, "2A") == 0) return A2_CAP;
    if (strcmp(code, "1A") == 0) return A1_CAP;
    return 0;
}

static const char *class_name(const char *code)
{
    if (strcmp(code, "SL") == 0) return "Sleeper";
    if (strcmp(code, "3A") == 0) return "AC 3 Tier";
    if (strcmp(code, "2A") == 0) return "AC 2 Tier";
    if (strcmp(code, "1A") == 0) return "AC First";
    return "Unknown";
}

static int confirmed_class_count(const char *code)
{
    int count = 0;
    Passenger *p;

    for (p = confirmed.front; p; p = p->next) {
        if (strcmp(p->travel_class, code) == 0)
            count++;
    }
    return count;
}

static int available_total(void)
{
    return MAX_CONFIRMED - confirmed.size;
}

static Passenger *find_ticket(int ticket_id)
{
    Passenger *p;

    for (p = confirmed.front; p; p = p->next)
        if (p->ticket_id == ticket_id) return p;

    for (p = waiting.front; p; p = p->next)
        if (p->ticket_id == ticket_id) return p;

    return NULL;
}

static Passenger *find_pnr(const char *pnr)
{
    Passenger *p;

    if (!pnr) return NULL;

    for (p = confirmed.front; p; p = p->next)
        if (strcmp(p->pnr, pnr) == 0) return p;

    for (p = waiting.front; p; p = p->next)
        if (strcmp(p->pnr, pnr) == 0) return p;

    return NULL;
}

static void make_pnr(char *out, size_t size)
{
    snprintf(out, size, "%010llu", next_pnr++);
}

static void make_seat(char *out, size_t size, const char *code)
{
    int capacity = class_capacity(code);
    int used[SL_CAP + 1] = {0};
    int seat_number;
    Passenger *p;

    for (p = confirmed.front; p; p = p->next) {
        if (strcmp(p->travel_class, code) == 0 &&
            strncmp(p->seat, code, strlen(code)) == 0) {
            int n = 0;
            if (sscanf(p->seat + strlen(code), "-%d", &n) == 1 &&
                n >= 1 && n <= capacity)
                used[n] = 1;
        }
    }

    for (seat_number = 1; seat_number <= capacity; seat_number++) {
        if (!used[seat_number]) {
            snprintf(out, size, "%s-%02d", code, seat_number);
            return;
        }
    }

    out[0] = '\0';
}

static int parse_ticket_id(const char *text, int *out)
{
    char *end;
    long value;

    if (!text || !out) return 0;

    errno = 0;
    value = strtol(text, &end, 10);

    while (*end == ' ' || *end == '\t')
        end++;

    if (text[0] == '\0' || *end != '\0' || errno == ERANGE ||
        value < 1 || value > INT_MAX)
        return 0;

    *out = (int)value;
    return 1;
}

static int valid_date(const char *text)
{
    int day, month, year;
    int max_day;

    if (!text || strlen(text) != 10)
        return 0;

    if (text[2] != '-' || text[5] != '-')
        return 0;

    if (sscanf(text, "%2d-%2d-%4d", &day, &month, &year) != 3)
        return 0;

    if (year < 2024 || month < 1 || month > 12 || day < 1)
        return 0;

    max_day = 31;
    if (month == 4 || month == 6 || month == 9 || month == 11)
        max_day = 30;
    else if (month == 2) {
        int leap = (year % 400 == 0) || (year % 4 == 0 && year % 100 != 0);
        max_day = leap ? 29 : 28;
    }

    return day <= max_day;
}

static int valid_phone(const char *text)
{
    size_t i;
    size_t digits = 0;

    if (!text) return 0;

    for (i = 0; text[i] != '\0'; i++) {
        if (isdigit((unsigned char)text[i]))
            digits++;
        else if (text[i] != '+' && text[i] != '-' && text[i] != ' ' && text[i] != '(' && text[i] != ')')
            return 0;
    }

    return digits >= 10 && digits <= 15;
}

static int valid_email(const char *text)
{
    const char *at;
    const char *dot;

    if (!text || text[0] == '\0') return 0;
    at = strchr(text, '@');
    dot = strrchr(text, '.');

    return at && dot && at < dot && at != text && dot[1] != '\0';
}

/* ---------- Logging ---------- */

static void append_activity(GtkTextBuffer *buffer, const char *line)
{
    GtkTextIter end;
    if (!buffer) return;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, line, -1);
}

static void log_msg(const char *fmt, ...)
{
    char msg[512];
    char line[640];
    char time_text[32];
    time_t now;
    struct tm *tm_info;
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    now = time(NULL);
    tm_info = localtime(&now);
    if (tm_info)
        strftime(time_text, sizeof time_text, "%H:%M:%S", tm_info);
    else
        snprintf(time_text, sizeof time_text, "--:--:--");

    snprintf(line, sizeof line, "%s %s\n", time_text, msg);

    append_activity(log_buffer, line);
    append_activity(dash_activity_buffer, line);

    if (lbl_status)
        gtk_label_set_text(GTK_LABEL(lbl_status), msg);
}

/* ---------- UI helpers ---------- */

static GtkWidget *make_label(const char *text, const char *class_name_text)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);

    if (class_name_text)
        gtk_style_context_add_class(gtk_widget_get_style_context(label), class_name_text);

    return label;
}

static GtkWidget *make_card(void)
{
    GtkWidget *box = gtk_event_box_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "card");
    return box;
}

static GtkWidget *make_stat_card(const char *icon,
                                const char *title,
                                GtkWidget **value,
                                const char *accent_class)
{
    GtkWidget *event = make_card();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *icon_label = make_label(icon, "stat-icon");
    GtkWidget *title_label = make_label(title, "stat-title");
    GtkWidget *number = make_label("0", "stat-value");

    gtk_style_context_add_class(gtk_widget_get_style_context(event), accent_class);
    gtk_container_add(GTK_CONTAINER(event), box);
    gtk_container_set_border_width(GTK_CONTAINER(box), 13);

    gtk_box_pack_start(GTK_BOX(top), icon_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), title_label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), top, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), number, FALSE, FALSE, 0);

    *value = number;
    return event;
}

static GtkWidget *make_class_card(const char *code, const char *name, int index)
{
    GtkWidget *event = make_card();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *code_label = make_label(code, "class-code");
    GtkWidget *name_label = make_label(name, "mini");
    GtkWidget *free_label = make_label("0 / 0 free", "class-free");
    GtkWidget *progress = gtk_progress_bar_new();

    gtk_style_context_add_class(gtk_widget_get_style_context(event), "class-card");
    gtk_container_add(GTK_CONTAINER(event), box);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);

    gtk_box_pack_start(GTK_BOX(head), code_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(head), name_label, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(head), free_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), head, FALSE, FALSE, 0);

    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progress), FALSE);
    gtk_box_pack_start(GTK_BOX(box), progress, FALSE, FALSE, 0);

    class_free[index] = free_label;
    class_progress[index] = progress;
    return event;
}

static GtkWidget *make_section_header(const char *title, const char *subtitle)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_box_pack_start(GTK_BOX(box), make_label(title, "section-title"), FALSE, FALSE, 0);
    if (subtitle)
        gtk_box_pack_start(GTK_BOX(box), make_label(subtitle, "section-subtitle"), FALSE, FALSE, 0);
    return box;
}

static GtkWidget *make_page(void)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(page), "page");
    return page;
}

static GtkWidget *make_field_label(const char *text)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_style_context_add_class(gtk_widget_get_style_context(label), "field-label");
    return label;
}

static GtkWidget *make_badge(const char *text, const char *class_name_text)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_style_context_add_class(gtk_widget_get_style_context(label), "badge");
    gtk_style_context_add_class(gtk_widget_get_style_context(label), class_name_text);
    return label;
}

static void set_detail_text(GtkWidget *label, const char *text)
{
    if (label)
        gtk_label_set_text(GTK_LABEL(label), text ? text : "—");
}

/* ---------- Dashboard ---------- */

static void refresh_dashboard(void)
{
    const char *codes[] = {"SL", "3A", "2A", "1A"};
    int i;
    char text[128];

    snprintf(text, sizeof text, "%d / %d", confirmed.size, MAX_CONFIRMED);
    gtk_label_set_text(GTK_LABEL(lbl_confirmed), text);

    snprintf(text, sizeof text, "%d / %d", waiting.size, MAX_WAITING);
    gtk_label_set_text(GTK_LABEL(lbl_waiting), text);

    snprintf(text, sizeof text, "%d", available_total());
    gtk_label_set_text(GTK_LABEL(lbl_available), text);

    snprintf(text, sizeof text, "%d", total_cancelled);
    gtk_label_set_text(GTK_LABEL(lbl_cancelled), text);

    snprintf(text, sizeof text, "%d", total_bookings);
    gtk_label_set_text(GTK_LABEL(dash_total), text);

    snprintf(text, sizeof text, "%d", total_promoted);
    gtk_label_set_text(GTK_LABEL(dash_promoted), text);

    for (i = 0; i < 4; i++) {
        int cap = class_capacity(codes[i]);
        int used = confirmed_class_count(codes[i]);
        int free_seats = cap - used;
        double fraction = cap > 0 ? (double)used / (double)cap : 0.0;
        char free_text[64];

        snprintf(free_text, sizeof free_text, "%d / %d free", free_seats, cap);
        gtk_label_set_text(GTK_LABEL(class_free[i]), free_text);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(class_progress[i]), fraction);
    }
}

static int passes_class_filter(const char *code)
{
    gchar *selected;
    int pass;

    if (!combo_filter_class) return 1;
    selected = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo_filter_class));
    if (!selected) return 1;

    pass = (strcmp(selected, "All Classes") == 0 || strcmp(selected, code) == 0);
    g_free(selected);
    return pass;
}

static int passes_status_filter(int is_waiting)
{
    gchar *selected;
    int pass;

    if (!combo_filter_status) return 1;
    selected = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo_filter_status));
    if (!selected) return 1;

    if (strcmp(selected, "All Status") == 0)
        pass = 1;
    else if (strcmp(selected, "Confirmed") == 0)
        pass = !is_waiting;
    else if (strcmp(selected, "Waiting") == 0)
        pass = is_waiting;
    else
        pass = 1;

    g_free(selected);
    return pass;
}

static void set_detail_status_text(const char *text, gboolean is_waiting)
{
    GtkStyleContext *ctx;

    if (!detail_status) return;

    gtk_label_set_text(GTK_LABEL(detail_status), text ? text : "—");
    ctx = gtk_widget_get_style_context(detail_status);
    gtk_style_context_remove_class(ctx, "status-confirmed");
    gtk_style_context_remove_class(ctx, "status-waiting");
    if (text && is_waiting)
        gtk_style_context_add_class(ctx, "status-waiting");
    else if (text)
        gtk_style_context_add_class(ctx, "status-confirmed");
}

static void refresh_selected_details_real(void)
{
    Passenger *p = NULL;
    char text[128];
    gboolean is_waiting;

    if (selected_ticket_id > 0)
        p = find_ticket(selected_ticket_id);

    if (!p) {
        set_detail_text(detail_name, "Select a passenger from the table");
        set_detail_text(detail_ticket, "—");
        set_detail_text(detail_pnr, "—");
        set_detail_text(detail_class, "—");
        set_detail_text(detail_seat, "—");
        set_detail_text(detail_date, "—");
        set_detail_text(detail_phone, "—");
        set_detail_text(detail_email, "—");
        set_detail_text(detail_berth, "—");
        set_detail_status_text("—", FALSE);
        return;
    }

    is_waiting = (p->seat[0] == '\0');
    set_detail_text(detail_name, p->name);
    snprintf(text, sizeof text, "%d", p->ticket_id);
    set_detail_text(detail_ticket, text);
    set_detail_text(detail_pnr, p->pnr);
    set_detail_text(detail_class, p->travel_class);
    set_detail_text(detail_seat, is_waiting ? "Waiting List" : p->seat);
    set_detail_text(detail_date, p->date);
    set_detail_text(detail_phone, p->phone);
    set_detail_text(detail_email, p->email);
    set_detail_text(detail_berth, p->berth);
    set_detail_status_text(is_waiting ? "WAITING" : "CONFIRMED", is_waiting);
}

/* Keep the public name small so refresh callers stay readable. */
static void refresh_details(void)
{
    refresh_selected_details_real();
}

static void clear_recent_list(void)
{
    GList *children;
    GList *iter;

    if (!recent_list) return;

    children = gtk_container_get_children(GTK_CONTAINER(recent_list));
    for (iter = children; iter; iter = iter->next)
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);
}

static int cmp_recent_desc(const void *a, const void *b)
{
    const Passenger *pa = *(const Passenger * const *)a;
    const Passenger *pb = *(const Passenger * const *)b;

    if (pa->ticket_id < pb->ticket_id) return 1;
    if (pa->ticket_id > pb->ticket_id) return -1;
    return 0;
}

static GtkWidget *make_recent_row(Passenger *p)
{
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *ticket_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *ticket = gtk_label_new(NULL);
    GtkWidget *name = gtk_label_new(p->name);
    GtkWidget *middle = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    GtkWidget *sub = gtk_label_new(NULL);
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    GtkWidget *status;
    GtkWidget *seat;
    char buf[128];
    gboolean is_waiting = (p->seat[0] == '\0');

    gtk_style_context_add_class(gtk_widget_get_style_context(row), "recent-row");
    gtk_container_add(GTK_CONTAINER(row), outer);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 10);

    snprintf(buf, sizeof buf, "#%d", p->ticket_id);
    gtk_label_set_text(GTK_LABEL(ticket), buf);
    gtk_label_set_xalign(GTK_LABEL(ticket), 0.0f);
    gtk_style_context_add_class(gtk_widget_get_style_context(ticket), "recent-ticket");
    gtk_box_pack_start(GTK_BOX(ticket_box), ticket, FALSE, FALSE, 0);

    gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
    gtk_style_context_add_class(gtk_widget_get_style_context(name), "recent-name");
    gtk_box_pack_start(GTK_BOX(middle), name, FALSE, FALSE, 0);

    snprintf(buf, sizeof buf, "PNR %s • %s • %s", p->pnr,
             p->travel_class, p->date);
    gtk_label_set_text(GTK_LABEL(sub), buf);
    gtk_label_set_xalign(GTK_LABEL(sub), 0.0f);
    gtk_style_context_add_class(gtk_widget_get_style_context(sub), "mini");
    gtk_box_pack_start(GTK_BOX(middle), sub, FALSE, FALSE, 0);
    gtk_widget_set_hexpand(middle, TRUE);

    status = make_badge(is_waiting ? "WAITING" : "CONFIRMED",
                        is_waiting ? "badge-waiting" : "badge-confirmed");
    gtk_box_pack_start(GTK_BOX(right), status, FALSE, FALSE, 0);

    seat = gtk_label_new(NULL);
    snprintf(buf, sizeof buf, "%s", is_waiting ? "WL queue" : p->seat);
    gtk_label_set_text(GTK_LABEL(seat), buf);
    gtk_label_set_xalign(GTK_LABEL(seat), 1.0f);
    gtk_style_context_add_class(gtk_widget_get_style_context(seat), "mini");
    gtk_box_pack_start(GTK_BOX(right), seat, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(outer), ticket_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), middle, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(outer), right, FALSE, FALSE, 0);

    g_object_set_data(G_OBJECT(row), "ticket-id", GINT_TO_POINTER(p->ticket_id));
    return row;
}

static void on_recent_activated(GtkListBox *list, GtkListBoxRow *row, gpointer data)
{
    int ticket_id;
    Passenger *p;

    (void)list;
    (void)data;
    if (!row) return;

    ticket_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "ticket-id"));
    p = find_ticket(ticket_id);
    if (p)
        show_ticket_dialog(p);
}

static void refresh_recent_list(void)
{
    Passenger *items[MAX_CONFIRMED + MAX_WAITING];
    Passenger *p;
    size_t count = 0;
    size_t i;
    GtkWidget *row;

    clear_recent_list();

    for (p = confirmed.front; p && count < G_N_ELEMENTS(items); p = p->next)
        items[count++] = p;
    for (p = waiting.front; p && count < G_N_ELEMENTS(items); p = p->next)
        items[count++] = p;

    if (count > 1)
        qsort(items, count, sizeof items[0], cmp_recent_desc);

    if (count == 0) {
        GtkWidget *empty = gtk_label_new("No bookings yet. Your latest reservations will appear here.");
        gtk_label_set_xalign(GTK_LABEL(empty), 0.0f);
        gtk_style_context_add_class(gtk_widget_get_style_context(empty), "empty-text");
        gtk_list_box_insert(recent_list, empty, -1);
    } else {
        size_t limit = count < 6 ? count : 6;
        for (i = 0; i < limit; i++) {
            row = make_recent_row(items[i]);
            gtk_list_box_insert(recent_list, row, -1);
        }
    }

    gtk_widget_show_all(GTK_WIDGET(recent_list));
}

static void refresh_tables(void)
{
    GtkTreeIter iter;
    Passenger *p;
    int position = 1;

    gtk_list_store_clear(store_confirmed);
    gtk_list_store_clear(store_waiting);

    if (passes_status_filter(0)) {
        for (p = confirmed.front; p; p = p->next) {
            if (!passes_class_filter(p->travel_class))
                continue;

            gtk_list_store_append(store_confirmed, &iter);
            gtk_list_store_set(store_confirmed, &iter,
                               0, p->ticket_id,
                               1, p->pnr,
                               2, p->name,
                               3, p->travel_class,
                               4, p->seat,
                               5, p->date,
                               6, "CONFIRMED",
                               -1);
        }
    }

    for (p = waiting.front; p; p = p->next, position++)
        p->waiting_position = position;

    if (passes_status_filter(1)) {
        position = 1;
        for (p = waiting.front; p; p = p->next, position++) {
            if (!passes_class_filter(p->travel_class))
                continue;

            gtk_list_store_append(store_waiting, &iter);
            gtk_list_store_set(store_waiting, &iter,
                               0, position,
                               1, p->ticket_id,
                               2, p->pnr,
                               3, p->name,
                               4, p->travel_class,
                               5, p->date,
                               6, "WAITING",
                               -1);
        }
    }

    refresh_dashboard();
    refresh_recent_list();
    refresh_details();
}

static void update_booking_availability(void)
{
    gchar *class_code;
    int cap;
    int used;
    int free_seats;
    char text[160];
    double fraction;

    if (!combo_class || !booking_availability_label || !booking_availability_progress)
        return;

    class_code = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo_class));
    if (!class_code) return;

    cap = class_capacity(class_code);
    used = confirmed_class_count(class_code);
    free_seats = cap - used;
    fraction = cap > 0 ? (double)used / (double)cap : 0.0;

    if (free_seats > 0) {
        snprintf(text, sizeof text,
                 "%s • %s • %d of %d seats currently available",
                 class_code, class_name(class_code), free_seats, cap);
        gtk_style_context_remove_class(
            gtk_widget_get_style_context(booking_availability_label), "warning-text");
        gtk_style_context_add_class(
            gtk_widget_get_style_context(booking_availability_label), "success-text");
    } else {
        snprintf(text, sizeof text,
                 "%s • %s • CLASS FULL — a new booking will enter the FIFO waiting queue",
                 class_code, class_name(class_code));
        gtk_style_context_remove_class(
            gtk_widget_get_style_context(booking_availability_label), "success-text");
        gtk_style_context_add_class(
            gtk_widget_get_style_context(booking_availability_label), "warning-text");
    }

    gtk_label_set_text(GTK_LABEL(booking_availability_label), text);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(booking_availability_progress), fraction);
    g_free(class_code);
}

static void clear_booking_fields(void)
{
    gtk_entry_set_text(GTK_ENTRY(entry_name), "");
    gtk_entry_set_text(GTK_ENTRY(entry_phone), "");
    gtk_entry_set_text(GTK_ENTRY(entry_email), "");
    gtk_entry_set_text(GTK_ENTRY(entry_date), "");
    gtk_widget_grab_focus(entry_name);
}

/* ---------- E-ticket generation, saving and dialog ---------- */

static gchar *ticket_text(Passenger *p)
{
    const char *status = p->seat[0] ? "CONFIRMED" : "WAITING";
    const char *seat_or_wl = p->seat[0] ? p->seat : "Waiting List";
    char wl_line[64] = "";

    if (!p->seat[0])
        snprintf(wl_line, sizeof wl_line, "Waiting Position : WL %d\n", p->waiting_position);

    return g_strdup_printf(
        "==================================================\n"
        "                RAILWAY E-TICKET                  \n"
        "==================================================\n"
        "PNR              : %s\n"
        "Ticket ID        : %d\n"
        "Status           : %s\n"
        "%s"
        "--------------------------------------------------\n"
        "Train            : Kerala Express\n"
        "Train No.        : 12625\n"
        "From             : %s\n"
        "To               : %s\n"
        "Journey Date     : %s\n"
        "Departure        : 12:30 PM\n"
        "Arrival          : 06:20 AM\n"
        "--------------------------------------------------\n"
        "Passenger        : %s\n"
        "Age / Gender     : %d / %s\n"
        "Phone            : %s\n"
        "Email            : %s\n"
        "Class            : %s (%s)\n"
        "Berth Preference : %s\n"
        "Seat             : %s\n"
        "==================================================\n"
        "Educational simulation: FIFO queue reservation.\n"
        "Berth preference does not guarantee a real railway\n"
        "coach/berth allocation in this academic project.\n"
        "==================================================\n",
        p->pnr, p->ticket_id, status, wl_line,
        p->from, p->to, p->date,
        p->name, p->age, p->gender, p->phone, p->email,
        p->travel_class, class_name(p->travel_class), p->berth, seat_or_wl);
}

static void save_ticket_to_file(Passenger *p, GtkWindow *parent)
{
    GtkWidget *dialog;
    GtkFileFilter *filter;
    gchar *text;
    gchar *path = NULL;
    GError *error = NULL;
    int response;
    char filename[64];

    if (!p) return;

    dialog = gtk_file_chooser_dialog_new(
        "Save Railway Ticket",
        parent,
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Save", GTK_RESPONSE_ACCEPT,
        NULL);

    snprintf(filename, sizeof filename, "Railway_Ticket_%d.txt", p->ticket_id);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), filename);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Text files");
    gtk_file_filter_add_pattern(filter, "*.txt");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_ACCEPT)
        path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

    gtk_widget_destroy(dialog);

    if (!path)
        return;

    text = ticket_text(p);
    if (!g_file_set_contents(path, text, -1, &error)) {
        log_msg("ERROR: could not save ticket: %s", error ? error->message : "unknown error");
        if (error) g_error_free(error);
    } else {
        log_msg("TICKET SAVED: #%d → %s", p->ticket_id, path);
        show_notification("Ticket saved successfully.", GTK_MESSAGE_INFO);
    }

    g_free(text);
    g_free(path);
}

static void show_ticket_dialog(Passenger *p)
{
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *top;
    GtkWidget *grid;
    GtkWidget *title;
    GtkWidget *status;
    GtkWidget *note;
    GtkWidget *copy_button;
    GtkWidget *save_button;
    GtkWidget *close_button;
    char text[256];
    int response;
    gboolean is_waiting;

    if (!p) return;

    is_waiting = (p->seat[0] == '\0');
    dialog = gtk_dialog_new();
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_title(GTK_WINDOW(dialog), is_waiting ? "Waiting List Ticket" : "Railway E-Ticket");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 560, 620);

    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 20);

    top = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    title = gtk_label_new("🚆 RAILWAY E-TICKET");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "ticket-title");
    gtk_box_pack_start(GTK_BOX(top), title, FALSE, FALSE, 0);

    status = gtk_label_new(is_waiting ? "WAITING LIST" : "CONFIRMED RESERVATION");
    gtk_style_context_add_class(gtk_widget_get_style_context(status),
                                is_waiting ? "status-waiting" : "status-confirmed");
    gtk_box_pack_start(GTK_BOX(top), status, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), top, FALSE, FALSE, 4);

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 18);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 16);
    gtk_style_context_add_class(gtk_widget_get_style_context(grid), "ticket-card");

#define ADD_TICKET_ROW(lbl, val) do { \
    GtkWidget *k = gtk_label_new(lbl); \
    GtkWidget *v = gtk_label_new(val); \
    gtk_label_set_xalign(GTK_LABEL(k), 0.0f); \
    gtk_label_set_xalign(GTK_LABEL(v), 0.0f); \
    gtk_style_context_add_class(gtk_widget_get_style_context(k), "ticket-key"); \
    gtk_style_context_add_class(gtk_widget_get_style_context(v), "ticket-value"); \
    gtk_grid_attach(GTK_GRID(grid), k, 0, ticket_row, 1, 1); \
    gtk_grid_attach(GTK_GRID(grid), v, 1, ticket_row, 1, 1); \
    ticket_row++; \
} while (0)

    {
        int ticket_row = 0;
        snprintf(text, sizeof text, "%d", p->ticket_id); ADD_TICKET_ROW("Ticket ID", text);
        ADD_TICKET_ROW("PNR", p->pnr);
        ADD_TICKET_ROW("Passenger", p->name);
        snprintf(text, sizeof text, "%d", p->age); ADD_TICKET_ROW("Age", text);
        ADD_TICKET_ROW("Gender", p->gender);
        ADD_TICKET_ROW("Phone", p->phone);
        ADD_TICKET_ROW("Email", p->email);
        ADD_TICKET_ROW("Class", class_name(p->travel_class));
        ADD_TICKET_ROW("Berth Preference", p->berth);
        ADD_TICKET_ROW("From", p->from);
        ADD_TICKET_ROW("To", p->to);
        ADD_TICKET_ROW("Journey Date", p->date);
        if (is_waiting) {
            snprintf(text, sizeof text, "WL %d", p->waiting_position);
            ADD_TICKET_ROW("Position", text);
        } else {
            ADD_TICKET_ROW("Seat", p->seat);
        }
    }
#undef ADD_TICKET_ROW

    gtk_box_pack_start(GTK_BOX(content), grid, FALSE, FALSE, 6);

    note = gtk_label_new("Keep your PNR or Ticket ID for quick search and cancellation.\nBerth preference is recorded but not guaranteed by this educational simulator.");
    gtk_label_set_line_wrap(GTK_LABEL(note), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(note), "ticket-note");
    gtk_box_pack_start(GTK_BOX(content), note, FALSE, FALSE, 8);

    copy_button = gtk_dialog_add_button(GTK_DIALOG(dialog), "Copy PNR", 1);
    save_button = gtk_dialog_add_button(GTK_DIALOG(dialog), "Save Ticket", 2);
    close_button = gtk_dialog_add_button(GTK_DIALOG(dialog), "Close", GTK_RESPONSE_CLOSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(copy_button), "soft");
    gtk_style_context_add_class(gtk_widget_get_style_context(save_button), "primary");
    (void)close_button;

    gtk_widget_show_all(dialog);

    for (;;) {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        if (response == 1) {
            GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
            gtk_clipboard_set_text(clipboard, p->pnr, -1);
            log_msg("PNR COPIED: %s", p->pnr);
            show_notification("PNR copied to clipboard.", GTK_MESSAGE_INFO);
        } else if (response == 2) {
            save_ticket_to_file(p, GTK_WINDOW(dialog));
        } else {
            break;
        }
    }

    gtk_widget_destroy(dialog);
}

/* ---------- Event handlers: booking, cancellation, search, reset ---------- */

static void on_book(GtkWidget *widget, gpointer data)
{
    gchar *gender = NULL;
    gchar *class_code = NULL;
    gchar *berth = NULL;
    const char *name;
    const char *phone;
    const char *email;
    const char *date;
    int age;
    int class_cap;
    int class_confirmed;
    Passenger *p;
    char clean_name[NAME_LEN];
    char clean_phone[PHONE_LEN];
    char clean_email[EMAIL_LEN];
    char clean_date[DATE_LEN];

    (void)widget;
    (void)data;

    name = gtk_entry_get_text(GTK_ENTRY(entry_name));
    phone = gtk_entry_get_text(GTK_ENTRY(entry_phone));
    email = gtk_entry_get_text(GTK_ENTRY(entry_email));
    date = gtk_entry_get_text(GTK_ENTRY(entry_date));
    age = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_age));
    gender = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo_gender));
    class_code = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo_class));
    berth = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo_berth));

    copy_text(clean_name, sizeof clean_name, name);
    copy_text(clean_phone, sizeof clean_phone, phone);
    copy_text(clean_email, sizeof clean_email, email);
    copy_text(clean_date, sizeof clean_date, date);
    trim_spaces(clean_name);
    trim_spaces(clean_phone);
    trim_spaces(clean_email);
    trim_spaces(clean_date);

    if (!gender || !class_code || !berth) {
        log_msg("ERROR: complete the selection fields before booking.");
        show_notification("Please complete the booking selections.", GTK_MESSAGE_WARNING);
        goto cleanup;
    }

    if (clean_name[0] == '\0') {
        log_msg("ERROR: enter passenger name.");
        show_notification("Passenger name is required.", GTK_MESSAGE_WARNING);
        goto cleanup;
    }

    if (!valid_phone(clean_phone)) {
        log_msg("ERROR: phone number must contain 10–15 digits.");
        show_notification("Enter a valid phone number.", GTK_MESSAGE_WARNING);
        goto cleanup;
    }

    if (!valid_email(clean_email)) {
        log_msg("ERROR: enter a valid email address.");
        show_notification("Enter a valid email address.", GTK_MESSAGE_WARNING);
        goto cleanup;
    }

    if (!valid_date(clean_date)) {
        log_msg("ERROR: journey date must use DD-MM-YYYY.");
        show_notification("Use a valid DD-MM-YYYY journey date.", GTK_MESSAGE_WARNING);
        goto cleanup;
    }

    if (confirmed.size >= MAX_CONFIRMED && waiting.size >= MAX_WAITING) {
        log_msg("TRAIN FULL: confirmed and waiting lists are full.");
        show_notification("Train and waiting list are full.", GTK_MESSAGE_ERROR);
        goto cleanup;
    }

    class_cap = class_capacity(class_code);
    class_confirmed = confirmed_class_count(class_code);

    p = calloc(1, sizeof *p);
    if (!p) {
        log_msg("ERROR: out of memory while creating passenger.");
        show_notification("Could not create the reservation.", GTK_MESSAGE_ERROR);
        goto cleanup;
    }

    p->ticket_id = next_ticket++;
    make_pnr(p->pnr, sizeof p->pnr);
    p->age = age;
    copy_text(p->name, sizeof p->name, clean_name);
    copy_text(p->gender, sizeof p->gender, gender);
    copy_text(p->phone, sizeof p->phone, clean_phone);
    copy_text(p->email, sizeof p->email, clean_email);
    copy_text(p->travel_class, sizeof p->travel_class, class_code);
    copy_text(p->berth, sizeof p->berth, berth);
    copy_text(p->from, sizeof p->from, "Thiruvananthapuram");
    copy_text(p->to, sizeof p->to, "New Delhi");
    copy_text(p->date, sizeof p->date, clean_date);

    if (confirmed.size < MAX_CONFIRMED && class_confirmed < class_cap) {
        make_seat(p->seat, sizeof p->seat, class_code);
        enqueue(&confirmed, p);
        total_bookings++;

        log_msg("CONFIRMED: #%d | PNR %s | %s | %s | Seat %s",
                p->ticket_id, p->pnr, p->name, class_code, p->seat);
        show_notification("Booking confirmed — seat allocated.", GTK_MESSAGE_INFO);
    } else {
        if (waiting.size >= MAX_WAITING) {
            free(p);
            log_msg("ERROR: waiting list is full.");
            show_notification("Waiting list is full.", GTK_MESSAGE_ERROR);
            goto cleanup;
        }

        p->seat[0] = '\0';
        p->waiting_position = waiting.size + 1;
        enqueue(&waiting, p);
        total_bookings++;

        log_msg("WAITING: #%d | PNR %s | %s | %s | WL %d",
                p->ticket_id, p->pnr, p->name, class_code, p->waiting_position);
        show_notification("Class full — passenger added to FIFO waiting list.", GTK_MESSAGE_WARNING);
    }

    selected_ticket_id = p->ticket_id;
    clear_booking_fields();
    refresh_tables();
    refresh_details();
    show_ticket_dialog(p);

cleanup:
    g_free(gender);
    g_free(class_code);
    g_free(berth);
}

static int ask_confirmation(const char *title, const char *secondary, GtkMessageType type)
{
    GtkWidget *dialog;
    gint response;

    if (!confirm_actions)
        return 1;

    dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                    GTK_DIALOG_MODAL,
                                    type,
                                    GTK_BUTTONS_YES_NO,
                                    "%s", title);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", secondary);
    response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return response == GTK_RESPONSE_YES;
}

static void cancel_ticket_id(int ticket_id)
{
    Passenger *candidate;
    Passenger *gone;
    Passenger *promoted;
    char old_seat[SEAT_LEN];
    char class_code[CLASS_LEN];

    candidate = find_ticket(ticket_id);
    if (!candidate) {
        log_msg("ERROR: ticket #%d was not found.", ticket_id);
        show_notification("Ticket ID was not found.", GTK_MESSAGE_ERROR);
        return;
    }

    if (candidate->seat[0] != '\0') {
        if (!ask_confirmation("Cancel this confirmed ticket?",
                              "The released seat may be assigned immediately to the first waiting passenger of the same class.",
                              GTK_MESSAGE_WARNING))
            return;

        gone = remove_ticket(&confirmed, ticket_id);
        if (!gone) {
            log_msg("ERROR: ticket #%d could not be removed safely.", ticket_id);
            show_notification("The ticket could not be cancelled.", GTK_MESSAGE_ERROR);
            return;
        }

        copy_text(old_seat, sizeof old_seat, gone->seat);
        copy_text(class_code, sizeof class_code, gone->travel_class);

        log_msg("CANCELLED: #%d | %s | Seat %s", gone->ticket_id, gone->name, old_seat);
        free(gone);
        total_cancelled++;

        promoted = remove_first_class(&waiting, class_code);
        if (promoted) {
            promoted->waiting_position = 0;
            copy_text(promoted->seat, sizeof promoted->seat, old_seat);
            enqueue(&confirmed, promoted);
            total_promoted++;

            log_msg("PROMOTED: #%d | %s | %s | Seat %s",
                    promoted->ticket_id, promoted->name,
                    promoted->travel_class, promoted->seat);
            show_notification("Waiting passenger automatically promoted.", GTK_MESSAGE_INFO);
        } else {
            log_msg("SEAT RELEASED: %s is now available.", old_seat);
            show_notification("Ticket cancelled and seat released.", GTK_MESSAGE_INFO);
        }
    } else {
        if (!ask_confirmation("Cancel this waiting-list ticket?",
                              "This waiting-list position will be removed and the remaining queue will move up.",
                              GTK_MESSAGE_WARNING))
            return;

        gone = remove_ticket(&waiting, ticket_id);
        if (!gone) {
            log_msg("ERROR: ticket #%d could not be removed safely.", ticket_id);
            show_notification("The waiting-list ticket could not be cancelled.", GTK_MESSAGE_ERROR);
            return;
        }

        log_msg("WAITING CANCELLED: #%d | %s | PNR %s",
                gone->ticket_id, gone->name, gone->pnr);
        free(gone);
        total_cancelled++;
        show_notification("Waiting-list ticket cancelled.", GTK_MESSAGE_INFO);
    }

    selected_ticket_id = -1;
    gtk_entry_set_text(GTK_ENTRY(entry_cancel), "");
    refresh_tables();
}

static void on_cancel(GtkWidget *widget, gpointer data)
{
    const char *text;
    int ticket_id;

    (void)widget;
    (void)data;

    text = gtk_entry_get_text(GTK_ENTRY(entry_cancel));
    if (!parse_ticket_id(text, &ticket_id)) {
        log_msg("ERROR: enter a valid numeric ticket ID.");
        show_notification("Enter a valid numeric ticket ID.", GTK_MESSAGE_WARNING);
        return;
    }

    cancel_ticket_id(ticket_id);
}

static void on_cancel_selected(GtkWidget *widget, gpointer data)
{
    Passenger *p;

    (void)widget;
    (void)data;

    p = find_ticket(selected_ticket_id);
    if (!p) {
        show_notification("Select a passenger first.", GTK_MESSAGE_WARNING);
        return;
    }

    cancel_ticket_id(p->ticket_id);
}

static void on_search(GtkWidget *widget, gpointer data)
{
    char clean[PNR_LEN];
    const char *query;
    int ticket_id;
    Passenger *p = NULL;

    (void)widget;
    (void)data;

    query = gtk_entry_get_text(GTK_ENTRY(entry_search));
    copy_text(clean, sizeof clean, query);
    trim_spaces(clean);

    if (clean[0] == '\0') {
        show_notification("Enter a PNR or Ticket ID to search.", GTK_MESSAGE_WARNING);
        return;
    }

    if (parse_ticket_id(clean, &ticket_id))
        p = find_ticket(ticket_id);
    if (!p)
        p = find_pnr(clean);

    if (!p) {
        log_msg("SEARCH: no ticket / PNR found for '%s'.", clean);
        show_notification("No matching ticket was found.", GTK_MESSAGE_ERROR);
        return;
    }

    selected_ticket_id = p->ticket_id;
    refresh_details();

    gtk_entry_set_text(GTK_ENTRY(entry_cancel), "");
    {
        char text[32];
        snprintf(text, sizeof text, "%d", p->ticket_id);
        gtk_entry_set_text(GTK_ENTRY(entry_cancel), text);
    }

    log_msg("SEARCH: FOUND #%d | %s | PNR %s", p->ticket_id, p->name, p->pnr);
    show_notification("Passenger found — details updated below.", GTK_MESSAGE_INFO);
}

static void on_confirmed_selected(GtkTreeSelection *selection, gpointer data)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    int ticket_id;
    char text[32];

    (void)data;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;

    gtk_tree_model_get(model, &iter, 0, &ticket_id, -1);
    selected_ticket_id = ticket_id;
    snprintf(text, sizeof text, "%d", ticket_id);
    gtk_entry_set_text(GTK_ENTRY(entry_cancel), text);
    refresh_details();
}

static void on_waiting_selected(GtkTreeSelection *selection, gpointer data)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    int ticket_id;
    char text[32];

    (void)data;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;

    gtk_tree_model_get(model, &iter, 1, &ticket_id, -1);
    selected_ticket_id = ticket_id;
    snprintf(text, sizeof text, "%d", ticket_id);
    gtk_entry_set_text(GTK_ENTRY(entry_cancel), text);
    refresh_details();
}

static void on_view_selected(GtkWidget *widget, gpointer data)
{
    Passenger *p;

    (void)widget;
    (void)data;

    p = find_ticket(selected_ticket_id);
    if (!p) {
        show_notification("Select a passenger first.", GTK_MESSAGE_WARNING);
        return;
    }
    show_ticket_dialog(p);
}

static void on_filter_changed(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    refresh_tables();
}

static void on_clear_log(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;

    gtk_text_buffer_set_text(log_buffer, "", -1);
    gtk_text_buffer_set_text(dash_activity_buffer, "", -1);
    gtk_label_set_text(GTK_LABEL(lbl_status), "Activity log cleared.");
}

static void clear_system_state(const char *log_text)
{
    free_queue(&confirmed);
    free_queue(&waiting);
    queue_init(&confirmed);
    queue_init(&waiting);

    next_ticket = 1001;
    next_pnr = 4817263951ULL;
    total_bookings = 0;
    total_cancelled = 0;
    total_promoted = 0;
    selected_ticket_id = -1;

    gtk_entry_set_text(GTK_ENTRY(entry_name), "");
    gtk_entry_set_text(GTK_ENTRY(entry_phone), "");
    gtk_entry_set_text(GTK_ENTRY(entry_email), "");
    gtk_entry_set_text(GTK_ENTRY(entry_date), "");
    gtk_entry_set_text(GTK_ENTRY(entry_search), "");
    gtk_entry_set_text(GTK_ENTRY(entry_cancel), "");

    gtk_text_buffer_set_text(log_buffer, "", -1);
    gtk_text_buffer_set_text(dash_activity_buffer, "", -1);
    refresh_tables();
    refresh_details();
    update_booking_availability();
    log_msg("%s", log_text);
}

static void on_reset(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;

    if (!ask_confirmation("Reset the complete reservation system?",
                          "All current bookings, counters, PNRs and waiting-list entries will be deleted.",
                          GTK_MESSAGE_WARNING))
        return;

    clear_system_state("SYSTEM RESET: a completely fresh reservation journey is ready.");
    show_notification("Reservation system reset.", GTK_MESSAGE_INFO);
}

static void on_new_journey(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;

    if (!ask_confirmation("Start a new journey?",
                          "All current passenger records will be cleared and the booking counters will restart.",
                          GTK_MESSAGE_QUESTION))
        return;

    clear_system_state("NEW JOURNEY: reservation system is ready for the next trip.");
    show_notification("New journey created.", GTK_MESSAGE_INFO);
}

/* ---------- Notifications ---------- */

static GtkWidget *notification_bar;
static GtkWidget *notification_label;
static guint notification_source = 0;

static gboolean hide_notification_cb(gpointer data)
{
    (void)data;
    if (notification_bar)
        gtk_widget_hide(notification_bar);
    notification_source = 0;
    return G_SOURCE_REMOVE;
}

static void show_notification(const char *message, GtkMessageType type)
{
    if (!notification_bar || !notification_label) return;

    if (notification_source != 0) {
        g_source_remove(notification_source);
        notification_source = 0;
    }

    gtk_info_bar_set_message_type(GTK_INFO_BAR(notification_bar), type);
    gtk_label_set_text(GTK_LABEL(notification_label), message);
    gtk_widget_show(notification_bar);
    notification_source = g_timeout_add(3200, hide_notification_cb, NULL);
}

/* ---------- Header clock ---------- */

static gboolean update_clock(gpointer data)
{
    char text[64];
    time_t now;
    struct tm *tm_info;

    (void)data;

    now = time(NULL);
    tm_info = localtime(&now);
    if (!tm_info || !header_clock)
        return G_SOURCE_CONTINUE;

    strftime(text, sizeof text, "%a %d %b • %I:%M:%S %p", tm_info);
    gtk_label_set_text(GTK_LABEL(header_clock), text);
    return G_SOURCE_CONTINUE;
}

/* ---------- Theme ---------- */

static void apply_theme(void)
{
    GtkStyleContext *ctx;

    if (!window) return;
    ctx = gtk_widget_get_style_context(window);

    if (dark_mode)
        gtk_style_context_add_class(ctx, "dark-mode");
    else
        gtk_style_context_remove_class(ctx, "dark-mode");
}

static void on_dark_mode_changed(GtkSwitch *sw, GParamSpec *pspec, gpointer data)
{
    (void)pspec;
    (void)data;

    dark_mode = gtk_switch_get_active(sw);
    apply_theme();
    log_msg("SETTINGS: dark mode %s.", dark_mode ? "enabled" : "disabled");
}

static void on_confirm_setting_changed(GtkSwitch *sw, GParamSpec *pspec, gpointer data)
{
    (void)pspec;
    (void)data;

    confirm_actions = gtk_switch_get_active(sw);
    log_msg("SETTINGS: confirmation dialogs %s.", confirm_actions ? "enabled" : "disabled");
}

/* ---------- Navigation ---------- */

static void go_to_page(const char *name)
{
    if (main_stack && name)
        gtk_stack_set_visible_child_name(GTK_STACK(main_stack), name);
}

static void on_go_booking(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    go_to_page("booking");
    gtk_widget_grab_focus(entry_name);
}

static void on_go_passengers(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    go_to_page("passengers");
}

static void on_go_activity(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    go_to_page("activity");
}

/* ---------- TreeView ---------- */

static GtkWidget *make_tree(GtkListStore *store,
                            const char **columns,
                            int count,
                            gboolean waiting_table)
{
    GtkWidget *tree;
    GtkTreeSelection *selection;
    int i;

    tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), TRUE);
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(tree), GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(tree), FALSE);
    gtk_tree_view_set_activate_on_single_click(GTK_TREE_VIEW(tree), FALSE);

    for (i = 0; i < count; i++) {
        GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *column =
            gtk_tree_view_column_new_with_attributes(columns[i], renderer, "text", i, NULL);

        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_column_set_sort_column_id(column, i);
        if (i == 2 || i == 5)
            gtk_tree_view_column_set_expand(column, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);
    }

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

    if (waiting_table)
        g_signal_connect(selection, "changed", G_CALLBACK(on_waiting_selected), NULL);
    else
        g_signal_connect(selection, "changed", G_CALLBACK(on_confirmed_selected), NULL);

    return tree;
}

/* ---------- CSS ---------- */

static void load_css(void)
{
    GString *css_builder = g_string_new(NULL);
    g_string_append(css_builder, "* { font-family: Sans; }");
    g_string_append(css_builder, "window { background: #eef2f7; color: #18233f; }");
    g_string_append(css_builder, "headerbar { background: #10245b; color: #ffffff; min-height: 72px; border: none; }");
    g_string_append(css_builder, "headerbar .title { color: #ffffff; font-size: 20px; font-weight: 900; }");
    g_string_append(css_builder, "headerbar .subtitle { color: #b9c9f3; font-size: 10px; }");
    g_string_append(css_builder, ".header-box { padding: 4px 8px; }");
    g_string_append(css_builder, ".header-clock { color: #dce7ff; font-size: 11px; font-weight: 700; }");
    g_string_append(css_builder, ".online { color: #63e6a5; font-weight: 900; font-size: 11px; }");
    g_string_append(css_builder, ".sidebar { background: #0c1735; padding: 12px 8px; border-right: 1px solid #20335f; }");
    g_string_append(css_builder, ".sidebar row { color: #d5e0fa; border-radius: 10px; padding: 11px 9px; margin: 3px 0; font-weight: 700; }");
    g_string_append(css_builder, ".sidebar row:hover { background: #162d61; color: #ffffff; }");
    g_string_append(css_builder, ".sidebar row:selected { background: #2856c7; color: #ffffff; }");
    g_string_append(css_builder, ".sidebar row label { color: inherit; font-size: 13px; font-weight: 800; }");
    g_string_append(css_builder, ".page { padding: 16px; background: #eef2f7; }");
    g_string_append(css_builder, ".hero { background: #162d70; border-radius: 18px; }");
    g_string_append(css_builder, ".hero-box { padding: 18px 22px; }");
    g_string_append(css_builder, ".hero-title { color: #ffffff; font-size: 26px; font-weight: 900; }");
    g_string_append(css_builder, ".hero-sub { color: #d6e1ff; font-size: 11px; }");
    g_string_append(css_builder, ".route-city { color: #ffffff; font-size: 13px; font-weight: 900; }");
    g_string_append(css_builder, ".route-time { color: #bdd0ff; font-size: 11px; }");
    g_string_append(css_builder, ".route-line { color: #84a4ff; font-size: 18px; font-weight: 900; }");
    g_string_append(css_builder, ".stat-title { color: #68748a; font-size: 10px; font-weight: 900; }");
    g_string_append(css_builder, ".stat-icon { font-size: 15px; }");
    g_string_append(css_builder, ".stat-value { color: #18233f; font-size: 25px; font-weight: 900; }");
    g_string_append(css_builder, ".card { background: #ffffff; border: 1px solid #dbe3ef; border-radius: 14px; }");
    g_string_append(css_builder, ".card:hover { border-color: #b9c8e4; }");
    g_string_append(css_builder, ".accent-blue { border-top: 3px solid #3159c7; }");
    g_string_append(css_builder, ".accent-amber { border-top: 3px solid #e0a11b; }");
    g_string_append(css_builder, ".accent-green { border-top: 3px solid #1e9d68; }");
    g_string_append(css_builder, ".accent-red { border-top: 3px solid #cc4638; }");
    g_string_append(css_builder, ".section-title { color: #18233f; font-size: 16px; font-weight: 900; }");
    g_string_append(css_builder, ".section-subtitle { color: #7b879b; font-size: 10px; }");
    g_string_append(css_builder, ".mini { color: #77839a; font-size: 10px; }");
    g_string_append(css_builder, ".class-card { background: #ffffff; border: 1px solid #dbe3ef; border-radius: 13px; }");
    g_string_append(css_builder, ".class-code { color: #18233f; font-size: 14px; font-weight: 900; }");
    g_string_append(css_builder, ".class-free { color: #73809a; font-size: 10px; font-weight: 700; }");
    g_string_append(css_builder, "progressbar trough { min-height: 8px; border-radius: 6px; background: #e6ebf3; }");
    g_string_append(css_builder, "progressbar progress { min-height: 8px; border-radius: 6px; background: #3159c7; }");
    g_string_append(css_builder, ".dark-progress progress { background: #6d8df5; }");
    g_string_append(css_builder, ".quick-title { color: #6c7890; font-size: 10px; font-weight: 900; }");
    g_string_append(css_builder, "button { border-radius: 8px; padding: 8px 14px; font-weight: 700; }");
    g_string_append(css_builder, "button:hover { background: #e7edfb; }");
    g_string_append(css_builder, ".primary { background: #2856c7; color: #ffffff; border: 1px solid #2856c7; font-weight: 900; padding: 10px 18px; }");
    g_string_append(css_builder, ".primary:hover { background: #1f47aa; color: #ffffff; }");
    g_string_append(css_builder, ".danger { background: #b72d22; color: #ffffff; border: 1px solid #b72d22; font-weight: 900; }");
    g_string_append(css_builder, ".danger:hover { background: #92231b; color: #ffffff; }");
    g_string_append(css_builder, ".soft { background: #e9efff; color: #24459d; border: 1px solid #cedcff; }");
    g_string_append(css_builder, ".soft:hover { background: #dce6ff; }");
    g_string_append(css_builder, ".ghost { background: transparent; color: #3159c7; border: 1px solid #cbd7ee; }");
    g_string_append(css_builder, ".form-card { background: #ffffff; border: 1px solid #dbe3ef; border-radius: 14px; padding: 16px; }");
    g_string_append(css_builder, ".field-label { color: #536078; font-size: 10px; font-weight: 900; }");
    g_string_append(css_builder, "entry { min-height: 36px; border-radius: 8px; padding: 5px 9px; }");
    g_string_append(css_builder, "combobox box { min-height: 34px; }");
    g_string_append(css_builder, "spinbutton { min-height: 34px; }");
    g_string_append(css_builder, ".availability-card { background: #f5f8ff; border: 1px solid #d5e0fb; border-radius: 12px; padding: 11px; }");
    g_string_append(css_builder, ".success-text { color: #18794f; font-weight: 800; font-size: 11px; }");
    g_string_append(css_builder, ".warning-text { color: #a96100; font-weight: 800; font-size: 11px; }");
    g_string_append(css_builder, ".recent-list { background: transparent; }");
    g_string_append(css_builder, ".recent-list row { background: #ffffff; border: 1px solid #dbe3ef; border-radius: 10px; margin: 3px 0; }");
    g_string_append(css_builder, ".recent-list row:hover { background: #f4f7fd; }");
    g_string_append(css_builder, ".recent-ticket { color: #3159c7; font-size: 11px; font-weight: 900; min-width: 56px; }");
    g_string_append(css_builder, ".recent-name { color: #18233f; font-size: 12px; font-weight: 900; }");
    g_string_append(css_builder, ".empty-text { color: #8a95a8; font-size: 11px; padding: 22px; }");
    g_string_append(css_builder, ".badge { border-radius: 999px; padding: 3px 8px; font-size: 9px; font-weight: 900; }");
    g_string_append(css_builder, ".badge-confirmed { background: #dff6ea; color: #14754e; }");
    g_string_append(css_builder, ".badge-waiting { background: #fff0cf; color: #9b5b00; }");
    g_string_append(css_builder, ".table-card { background: #ffffff; border: 1px solid #dbe3ef; border-radius: 14px; padding: 10px; }");
    g_string_append(css_builder, "treeview { background: #ffffff; color: #1c2741; border: none; }");
    g_string_append(css_builder, "treeview header button { color: #46526a; font-weight: 900; background: #f5f7fb; }");
    g_string_append(css_builder, "treeview.view row:selected { background: #dce7ff; color: #17294f; }");
    g_string_append(css_builder, ".detail-card { background: #f8faff; border: 1px solid #d9e2f3; border-radius: 14px; padding: 14px; }");
    g_string_append(css_builder, ".detail-name { color: #18233f; font-size: 18px; font-weight: 900; }");
    g_string_append(css_builder, ".detail-key { color: #7a869a; font-size: 9px; font-weight: 900; }");
    g_string_append(css_builder, ".detail-value { color: #26324a; font-size: 11px; font-weight: 800; }");
    g_string_append(css_builder, ".status-confirmed { color: #16754d; font-weight: 900; }");
    g_string_append(css_builder, ".status-waiting { color: #a35d00; font-weight: 900; }");
    g_string_append(css_builder, ".activity-view { background: #0a1329; color: #dce7ff; padding: 10px; border-radius: 10px; }");
    g_string_append(css_builder, ".activity-title { color: #ffffff; font-size: 11px; font-weight: 900; }");
    g_string_append(css_builder, ".ticket-title { color: #152a6e; font-size: 21px; font-weight: 900; }");
    g_string_append(css_builder, ".ticket-card { background: #f7f9fd; border: 1px solid #dfe6f1; border-radius: 13px; padding: 12px; }");
    g_string_append(css_builder, ".ticket-key { color: #78849a; font-size: 10px; font-weight: 900; }");
    g_string_append(css_builder, ".ticket-value { color: #1e2a44; font-size: 11px; font-weight: 800; }");
    g_string_append(css_builder, ".ticket-note { color: #728098; font-size: 10px; }");
    g_string_append(css_builder, ".setting-card { background: #ffffff; border: 1px solid #dbe3ef; border-radius: 14px; padding: 15px; }");
    g_string_append(css_builder, ".setting-title { color: #18233f; font-size: 13px; font-weight: 900; }");
    g_string_append(css_builder, ".setting-desc { color: #7a869a; font-size: 10px; }");
    g_string_append(css_builder, ".footer { color: #7e8a9e; font-size: 9px; padding: 2px; }");
    g_string_append(css_builder, ".infobar { border-radius: 9px; margin: 8px 12px; }");
    g_string_append(css_builder, "window.dark-mode { background: #0c1220; color: #e7ecf8; }");
    g_string_append(css_builder, "window.dark-mode .page { background: #101726; }");
    g_string_append(css_builder, "window.dark-mode .card, window.dark-mode .form-card, window.dark-mode .table-card, window.dark-mode .detail-card, window.dark-mode .setting-card { background: #182235; border-color: #2a3852; }");
    g_string_append(css_builder, "window.dark-mode .section-title, window.dark-mode .detail-name, window.dark-mode .setting-title, window.dark-mode .stat-value, window.dark-mode .class-code, window.dark-mode .recent-name { color: #eef3ff; }");
    g_string_append(css_builder, "window.dark-mode .mini, window.dark-mode .section-subtitle, window.dark-mode .detail-key, window.dark-mode .field-label, window.dark-mode .setting-desc { color: #9eabc2; }");
    g_string_append(css_builder, "window.dark-mode entry, window.dark-mode spinbutton, window.dark-mode combobox box { background: #111a2c; color: #edf2ff; border-color: #33425f; }");
    g_string_append(css_builder, "window.dark-mode treeview { background: #182235; color: #e9efff; }");
    g_string_append(css_builder, "window.dark-mode treeview header button { background: #121a2b; color: #cdd8ee; }");
    g_string_append(css_builder, "window.dark-mode treeview.view row:selected { background: #294b8f; color: #ffffff; }");
    g_string_append(css_builder, "window.dark-mode .recent-list row { background: #182235; border-color: #2a3852; }");
    g_string_append(css_builder, "window.dark-mode .availability-card { background: #15223b; border-color: #31476f; }");
    g_string_append(css_builder, "window.dark-mode .detail-value { color: #dae3f5; }");
    g_string_append(css_builder, "window.dark-mode .ticket-card { background: #182235; border-color: #2a3852; }");
    g_string_append(css_builder, "window.dark-mode .ticket-key { color: #9eabc2; }");
    g_string_append(css_builder, "window.dark-mode .ticket-value { color: #edf2ff; }");
    g_string_append(css_builder, "window.dark-mode .ticket-title { color: #b8cbff; }");

    const char *css = css_builder->str;
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
    g_string_free(css_builder, TRUE);
}

/* ---------- Widget builders ---------- */

static GtkWidget *make_info_pair(const char *key, GtkWidget **value_out)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *k = make_label(key, "detail-key");
    GtkWidget *v = make_label("—", "detail-value");
    gtk_box_pack_start(GTK_BOX(box), k, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), v, FALSE, FALSE, 0);
    *value_out = v;
    return box;
}

static GtkWidget *make_route_card(void)
{
    GtkWidget *card = make_card();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *center = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    gtk_container_add(GTK_CONTAINER(card), box);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);

    gtk_box_pack_start(GTK_BOX(left), make_label("THIRUVANANTHAPURAM", "route-city"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left), make_label("12:30 PM • Departure", "route-time"), FALSE, FALSE, 0);

    gtk_widget_set_hexpand(center, TRUE);
    {
        GtkWidget *route_line = make_label("● ───────── ◆ ───────── ●", "route-line");
        gtk_label_set_line_wrap(GTK_LABEL(route_line), TRUE);
        gtk_box_pack_start(GTK_BOX(center), route_line, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(center), make_label("KERALA EXPRESS • 12625", "mini"), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(right), make_label("NEW DELHI", "route-city"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right), make_label("06:20 AM • Arrival", "route-time"), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), left, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), center, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(box), right, FALSE, FALSE, 0);
    return card;
}

/* ---------- Window construction ---------- */

static void build_dashboard_page(GtkWidget *stack)
{
    GtkWidget *page = make_page();
    GtkWidget *hero;
    GtkWidget *hero_box;
    GtkWidget *title;
    GtkWidget *subtitle;
    GtkWidget *stats;
    GtkWidget *mini_stats;
    GtkWidget *class_row;
    GtkWidget *section_row;
    GtkWidget *recent_card;
    GtkWidget *recent_box;
    GtkWidget *recent_title;
    GtkWidget *recent_sw;
    GtkWidget *activity_card;
    GtkWidget *activity_box;
    GtkWidget *activity_title;
    GtkWidget *activity_sw;
    GtkWidget *quick;
    GtkWidget *quick_title;
    GtkWidget *quick_buttons;
    GtkWidget *button;
    GtkWidget *footer;

    hero = gtk_event_box_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(hero), "hero");
    hero_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(hero), hero_box);
    gtk_container_set_border_width(GTK_CONTAINER(hero_box), 18);

    title = make_label("🚆 KERALA EXPRESS", "hero-title");
    subtitle = make_label("Train 12625 • Queue-based reservation management • System Online", "hero-sub");
    gtk_box_pack_start(GTK_BOX(hero_box), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hero_box), subtitle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hero_box), make_route_card(), FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(page), hero, FALSE, FALSE, 0);

    stats = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_set_homogeneous(GTK_BOX(stats), TRUE);
    gtk_box_pack_start(GTK_BOX(stats), make_stat_card("✓", "CONFIRMED", &lbl_confirmed, "accent-blue"), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(stats), make_stat_card("◌", "WAITING", &lbl_waiting, "accent-amber"), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(stats), make_stat_card("▣", "AVAILABLE", &lbl_available, "accent-green"), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(stats), make_stat_card("×", "CANCELLED", &lbl_cancelled, "accent-red"), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), stats, FALSE, FALSE, 0);

    mini_stats = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_set_homogeneous(GTK_BOX(mini_stats), TRUE);
    gtk_box_pack_start(GTK_BOX(mini_stats), make_stat_card("Σ", "TOTAL BOOKINGS", &dash_total, "accent-blue"), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(mini_stats), make_stat_card("↗", "AUTO PROMOTED", &dash_promoted, "accent-green"), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), mini_stats, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(page),
                       make_section_header("Coach / Class Availability",
                                           "Live occupancy across the configured 38-seat train capacity."),
                       FALSE, FALSE, 0);

    class_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_set_homogeneous(GTK_BOX(class_row), TRUE);
    gtk_box_pack_start(GTK_BOX(class_row), make_class_card("SL", "Sleeper", 0), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(class_row), make_class_card("3A", "AC 3 Tier", 1), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(class_row), make_class_card("2A", "AC 2 Tier", 2), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(class_row), make_class_card("1A", "AC First", 3), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), class_row, FALSE, FALSE, 0);

    section_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_set_homogeneous(GTK_BOX(section_row), TRUE);

    recent_card = make_card();
    recent_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
    gtk_container_add(GTK_CONTAINER(recent_card), recent_box);
    gtk_container_set_border_width(GTK_CONTAINER(recent_box), 12);
    recent_title = make_label("Recent Bookings", "section-title");
    gtk_box_pack_start(GTK_BOX(recent_box), recent_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(recent_box), make_label("Click a reservation to open the e-ticket.", "section-subtitle"), FALSE, FALSE, 0);
    recent_list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(recent_list)), "recent-list");
    gtk_list_box_set_selection_mode(recent_list, GTK_SELECTION_NONE);
    g_signal_connect(recent_list, "row-activated", G_CALLBACK(on_recent_activated), NULL);
    recent_sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(recent_sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(recent_sw, TRUE);
    gtk_widget_set_size_request(recent_sw, -1, 185);
    gtk_container_add(GTK_CONTAINER(recent_sw), GTK_WIDGET(recent_list));
    gtk_box_pack_start(GTK_BOX(recent_box), recent_sw, TRUE, TRUE, 0);

    activity_card = make_card();
    activity_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
    gtk_container_add(GTK_CONTAINER(activity_card), activity_box);
    gtk_container_set_border_width(GTK_CONTAINER(activity_box), 12);
    activity_title = make_label("Recent Activity", "section-title");
    gtk_box_pack_start(GTK_BOX(activity_box), activity_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(activity_box), make_label("Live queue events, reservations and promotions.", "section-subtitle"), FALSE, FALSE, 0);
    {
        GtkWidget *view = gtk_text_view_new();
        GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
        gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
        gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
        gtk_widget_set_name(view, "dashboard-activity");
        gtk_style_context_add_class(gtk_widget_get_style_context(view), "activity-view");
        dash_activity_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
        gtk_widget_set_vexpand(scroll, TRUE);
        gtk_widget_set_size_request(scroll, -1, 185);
        gtk_container_add(GTK_CONTAINER(scroll), view);
        gtk_box_pack_start(GTK_BOX(activity_box), scroll, TRUE, TRUE, 0);
        activity_sw = scroll;
    }
    (void)activity_sw;

    gtk_box_pack_start(GTK_BOX(section_row), recent_card, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(section_row), activity_card, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), section_row, TRUE, TRUE, 0);

    quick = make_card();
    {
        GtkWidget *qb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_container_add(GTK_CONTAINER(quick), qb);
        gtk_container_set_border_width(GTK_CONTAINER(qb), 10);
        quick_title = make_label("QUICK ACTIONS", "quick-title");
        quick_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
        gtk_widget_set_hexpand(quick_buttons, TRUE);

        button = gtk_button_new_with_label("🚆 New Booking");
        gtk_style_context_add_class(gtk_widget_get_style_context(button), "primary");
        g_signal_connect(button, "clicked", G_CALLBACK(on_go_booking), NULL);
        gtk_box_pack_start(GTK_BOX(quick_buttons), button, FALSE, FALSE, 0);

        button = gtk_button_new_with_label("🚆 Manage Passengers");
        gtk_style_context_add_class(gtk_widget_get_style_context(button), "soft");
        g_signal_connect(button, "clicked", G_CALLBACK(on_go_passengers), NULL);
        gtk_box_pack_start(GTK_BOX(quick_buttons), button, FALSE, FALSE, 0);

        button = gtk_button_new_with_label("🚆 Open Activity");
        gtk_style_context_add_class(gtk_widget_get_style_context(button), "soft");
        g_signal_connect(button, "clicked", G_CALLBACK(on_go_activity), NULL);
        gtk_box_pack_start(GTK_BOX(quick_buttons), button, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(qb), quick_title, FALSE, FALSE, 4);
        gtk_box_pack_start(GTK_BOX(qb), quick_buttons, TRUE, TRUE, 0);
    }
    gtk_box_pack_start(GTK_BOX(page), quick, FALSE, FALSE, 0);

    footer = make_label("FIFO queue engine • GTK3 • Seat capacities: SL 20 / 3A 10 / 2A 5 / 1A 3 • Waiting limit: 10", "footer");
    gtk_label_set_xalign(GTK_LABEL(footer), 0.5f);
    gtk_box_pack_start(GTK_BOX(page), footer, FALSE, FALSE, 0);

    gtk_stack_add_titled(GTK_STACK(stack), page, "dashboard", "🚆 Dashboard");
}

static void build_booking_page(GtkWidget *stack)
{
    GtkWidget *page = make_page();
    GtkWidget *header;
    GtkWidget *content_row;
    GtkWidget *form_card;
    GtkWidget *journey_card;
    GtkWidget *grid;
    GtkWidget *book_button;
    GtkWidget *availability_card;
    GtkWidget *availability_box;
    GtkWidget *tip_card;
    GtkWidget *tip_box;
    int row = 0;

    header = make_section_header("Create a New Reservation",
                                 "Complete passenger and journey details. The queue engine decides confirmed or waiting status automatically.");
    gtk_box_pack_start(GTK_BOX(page), header, FALSE, FALSE, 0);

    content_row = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_wide_handle(GTK_PANED(content_row), TRUE);
    gtk_widget_set_hexpand(content_row, TRUE);
    gtk_widget_set_vexpand(content_row, FALSE);

    form_card = make_card();
    gtk_style_context_add_class(gtk_widget_get_style_context(form_card), "form-card");
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 9);
    gtk_container_add(GTK_CONTAINER(form_card), grid);

    gtk_grid_attach(GTK_GRID(grid), make_field_label("Full Name"), 0, row, 1, 1);
    entry_name = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_name), "Passenger full name");
    gtk_widget_set_hexpand(entry_name, TRUE);
    gtk_grid_attach(GTK_GRID(grid), entry_name, 1, row, 3, 1);
    gtk_grid_attach(GTK_GRID(grid), make_field_label("Age"), 4, row, 1, 1);
    spin_age = gtk_spin_button_new_with_range(1, 120, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_age), 25);
    gtk_grid_attach(GTK_GRID(grid), spin_age, 5, row, 1, 1);
    row++;

    gtk_grid_attach(GTK_GRID(grid), make_field_label("Gender"), 0, row, 1, 1);
    combo_gender = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_gender), "Male");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_gender), "Female");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_gender), "Other");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo_gender), 0);
    gtk_grid_attach(GTK_GRID(grid), combo_gender, 1, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), make_field_label("Phone"), 2, row, 1, 1);
    entry_phone = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_phone), "10–15 digit phone");
    gtk_grid_attach(GTK_GRID(grid), entry_phone, 3, row, 3, 1);
    row++;

    gtk_grid_attach(GTK_GRID(grid), make_field_label("Email"), 0, row, 1, 1);
    entry_email = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_email), "Passenger email address");
    gtk_grid_attach(GTK_GRID(grid), entry_email, 1, row, 5, 1);
    row++;

    gtk_grid_attach(GTK_GRID(grid), make_field_label("Class"), 0, row, 1, 1);
    combo_class = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_class), "SL");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_class), "3A");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_class), "2A");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_class), "1A");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo_class), 0);
    gtk_grid_attach(GTK_GRID(grid), combo_class, 1, row, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), make_field_label("Berth Preference"), 2, row, 1, 1);
    combo_berth = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_berth), "No Preference");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_berth), "Lower");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_berth), "Middle");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_berth), "Upper");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_berth), "Side Lower");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_berth), "Side Upper");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo_berth), 0);
    gtk_grid_attach(GTK_GRID(grid), combo_berth, 3, row, 3, 1);
    row++;

    gtk_grid_attach(GTK_GRID(grid), make_field_label("Journey Date"), 0, row, 1, 1);
    entry_date = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_date), "DD-MM-YYYY");
    gtk_entry_set_max_length(GTK_ENTRY(entry_date), 10);
    gtk_grid_attach(GTK_GRID(grid), entry_date, 1, row, 1, 1);
    book_button = gtk_button_new_with_label("🚆 CONFIRM BOOKING");
    gtk_style_context_add_class(gtk_widget_get_style_context(book_button), "primary");
    gtk_widget_set_hexpand(book_button, TRUE);
    gtk_grid_attach(GTK_GRID(grid), book_button, 2, row, 4, 1);
    row++;

    availability_card = make_card();
    gtk_style_context_add_class(gtk_widget_get_style_context(availability_card), "availability-card");
    availability_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_add(GTK_CONTAINER(availability_card), availability_box);
    gtk_container_set_border_width(GTK_CONTAINER(availability_box), 10);
    booking_availability_label = make_label("", "success-text");
    booking_availability_progress = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(availability_box), make_label("LIVE CLASS AVAILABILITY", "quick-title"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(availability_box), booking_availability_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(availability_box), booking_availability_progress, FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(grid), availability_card, 0, row, 6, 1);

    gtk_paned_pack1(GTK_PANED(content_row), form_card, TRUE, FALSE);

    journey_card = make_card();
    {
        GtkWidget *jb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        GtkWidget *route = make_route_card();
        GtkWidget *line1 = make_label("Train information", "section-title");
        GtkWidget *line2 = make_label("Kerala Express (12625)", "setting-title");
        GtkWidget *line3 = make_label("Departure 12:30 PM • Arrival 06:20 AM", "mini");
        GtkWidget *line4 = make_label("Route: Thiruvananthapuram → New Delhi", "mini");
        GtkWidget *line5 = make_label("Configured capacity", "section-title");
        GtkWidget *line6 = make_label("SL 20 • 3A 10 • 2A 5 • 1A 3 • Waiting 10", "mini");
        GtkWidget *line7 = make_label("Booking logic", "section-title");
        GtkWidget *line8 = make_label("Confirmed seats are assigned first. When a class is full, new passengers enter the FIFO waiting queue.", "mini");
        gtk_label_set_line_wrap(GTK_LABEL(line8), TRUE);
        gtk_label_set_line_wrap(GTK_LABEL(line4), TRUE);

        gtk_container_add(GTK_CONTAINER(journey_card), jb);
        gtk_container_set_border_width(GTK_CONTAINER(jb), 12);
        gtk_box_pack_start(GTK_BOX(jb), line1, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(jb), line2, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(jb), line3, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(jb), line4, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(jb), route, FALSE, FALSE, 3);
        gtk_box_pack_start(GTK_BOX(jb), line5, FALSE, FALSE, 2);
        gtk_box_pack_start(GTK_BOX(jb), line6, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(jb), line7, FALSE, FALSE, 2);
        gtk_box_pack_start(GTK_BOX(jb), line8, FALSE, FALSE, 0);
    }
    gtk_paned_pack2(GTK_PANED(content_row), journey_card, TRUE, FALSE);
    gtk_paned_set_position(GTK_PANED(content_row), 560);
    gtk_box_pack_start(GTK_BOX(page), content_row, FALSE, FALSE, 0);

    tip_card = make_card();
    tip_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_add(GTK_CONTAINER(tip_card), tip_box);
    gtk_container_set_border_width(GTK_CONTAINER(tip_box), 10);
    gtk_box_pack_start(GTK_BOX(tip_box), make_label("TIP", "badge"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tip_box), make_label("After booking, an e-ticket opens automatically. You can copy the PNR or save the ticket as a text file.", "mini"), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), tip_card, FALSE, FALSE, 0);

    g_signal_connect(book_button, "clicked", G_CALLBACK(on_book), NULL);
    g_signal_connect(entry_name, "activate", G_CALLBACK(on_book), NULL);
    g_signal_connect(combo_class, "changed", G_CALLBACK(update_booking_availability), NULL);

    gtk_stack_add_titled(GTK_STACK(stack), page, "booking", "🚆 Book Ticket");
}

static void build_passenger_page(GtkWidget *stack)
{
    GtkWidget *page = make_page();
    GtkWidget *header;
    GtkWidget *search_card;
    GtkWidget *search_box;
    GtkWidget *search_button;
    GtkWidget *cancel_button;
    GtkWidget *filters;
    GtkWidget *filter_label;
    GtkWidget *tables_row;
    GtkWidget *conf_card;
    GtkWidget *wait_card;
    GtkWidget *conf_sw;
    GtkWidget *wait_sw;
    GtkWidget *conf_tree;
    GtkWidget *wait_tree;
    GtkWidget *details_card;
    GtkWidget *details_box;
    GtkWidget *details_top;
    GtkWidget *details_grid;
    GtkWidget *view_button;
    GtkWidget *cancel_selected_button;
    const char *conf_cols[] = {"Ticket", "PNR", "Passenger", "Class", "Seat", "Date", "Status"};
    const char *wait_cols[] = {"WL", "Ticket", "PNR", "Passenger", "Class", "Date", "Status"};

    header = make_section_header("Passenger Management",
                                 "Search, filter, inspect and cancel current reservations from one place.");
    gtk_box_pack_start(GTK_BOX(page), header, FALSE, FALSE, 0);

    search_card = make_card();
    search_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_add(GTK_CONTAINER(search_card), search_box);
    gtk_container_set_border_width(GTK_CONTAINER(search_box), 10);

    entry_search = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_search), "Search by PNR or Ticket ID...");
    gtk_widget_set_hexpand(entry_search, TRUE);
    search_button = gtk_button_new_with_label("🚆 Search");
    gtk_style_context_add_class(gtk_widget_get_style_context(search_button), "soft");

    entry_cancel = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_cancel), "Ticket ID to cancel");
    gtk_widget_set_hexpand(entry_cancel, TRUE);
    cancel_button = gtk_button_new_with_label("✕ Cancel Ticket");
    gtk_style_context_add_class(gtk_widget_get_style_context(cancel_button), "danger");

    gtk_box_pack_start(GTK_BOX(search_box), entry_search, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(search_box), search_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(search_box), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(search_box), entry_cancel, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(search_box), cancel_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), search_card, FALSE, FALSE, 0);

    filters = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    filter_label = make_label("FILTERS", "quick-title");
    combo_filter_class = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_filter_class), "All Classes");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_filter_class), "SL");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_filter_class), "3A");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_filter_class), "2A");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_filter_class), "1A");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo_filter_class), 0);

    combo_filter_status = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_filter_status), "All Status");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_filter_status), "Confirmed");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_filter_status), "Waiting");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo_filter_status), 0);

    gtk_box_pack_start(GTK_BOX(filters), filter_label, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(filters), combo_filter_class, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(filters), combo_filter_status, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(filters), make_label("Click a row to load the selected passenger details below.", "mini"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), filters, FALSE, FALSE, 0);

    store_confirmed = gtk_list_store_new(7,
                                         G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING,
                                         G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                         G_TYPE_STRING);
    store_waiting = gtk_list_store_new(7,
                                       G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING,
                                       G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                       G_TYPE_STRING);

    tables_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_set_homogeneous(GTK_BOX(tables_row), TRUE);

    conf_card = make_card();
    {
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_container_add(GTK_CONTAINER(conf_card), box);
        gtk_container_set_border_width(GTK_CONTAINER(box), 9);
        gtk_box_pack_start(GTK_BOX(box), make_label("CONFIRMED PASSENGERS", "section-title"), FALSE, FALSE, 0);
        conf_sw = gtk_scrolled_window_new(NULL, NULL);
        gtk_widget_set_vexpand(conf_sw, TRUE);
        conf_tree = make_tree(store_confirmed, conf_cols, 7, FALSE);
        gtk_container_add(GTK_CONTAINER(conf_sw), conf_tree);
        gtk_box_pack_start(GTK_BOX(box), conf_sw, TRUE, TRUE, 0);
    }

    wait_card = make_card();
    {
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_container_add(GTK_CONTAINER(wait_card), box);
        gtk_container_set_border_width(GTK_CONTAINER(box), 9);
        gtk_box_pack_start(GTK_BOX(box), make_label("FIFO WAITING LIST", "section-title"), FALSE, FALSE, 0);
        wait_sw = gtk_scrolled_window_new(NULL, NULL);
        gtk_widget_set_vexpand(wait_sw, TRUE);
        wait_tree = make_tree(store_waiting, wait_cols, 7, TRUE);
        gtk_container_add(GTK_CONTAINER(wait_sw), wait_tree);
        gtk_box_pack_start(GTK_BOX(box), wait_sw, TRUE, TRUE, 0);
    }

    gtk_box_pack_start(GTK_BOX(tables_row), conf_card, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(tables_row), wait_card, TRUE, TRUE, 0);
    gtk_widget_set_vexpand(tables_row, TRUE);
    gtk_box_pack_start(GTK_BOX(page), tables_row, TRUE, TRUE, 0);

    details_card = make_card();
    gtk_style_context_add_class(gtk_widget_get_style_context(details_card), "detail-card");
    details_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
    gtk_container_add(GTK_CONTAINER(details_card), details_box);
    gtk_container_set_border_width(GTK_CONTAINER(details_box), 10);

    details_top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    details_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(details_grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(details_grid), 18);
    gtk_widget_set_hexpand(details_grid, TRUE);
    gtk_box_pack_start(GTK_BOX(details_top), details_grid, TRUE, TRUE, 0);

    {
        GtkWidget *name_label = make_label("Selected Passenger", "detail-name");
        gtk_grid_attach(GTK_GRID(details_grid), name_label, 0, 0, 2, 1);
        detail_name = name_label;
    }
    gtk_grid_attach(GTK_GRID(details_grid), make_info_pair("Ticket ID", &detail_ticket), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(details_grid), make_info_pair("PNR", &detail_pnr), 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(details_grid), make_info_pair("Class", &detail_class), 2, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(details_grid), make_info_pair("Seat / Queue", &detail_seat), 3, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(details_grid), make_info_pair("Status", &detail_status), 4, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(details_grid), make_info_pair("Journey Date", &detail_date), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(details_grid), make_info_pair("Phone", &detail_phone), 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(details_grid), make_info_pair("Email", &detail_email), 2, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(details_grid), make_info_pair("Berth", &detail_berth), 3, 2, 1, 1);

    view_button = gtk_button_new_with_label("🚆 View E-Ticket");
    gtk_style_context_add_class(gtk_widget_get_style_context(view_button), "primary");
    cancel_selected_button = gtk_button_new_with_label("✕ Cancel Selected");
    gtk_style_context_add_class(gtk_widget_get_style_context(cancel_selected_button), "danger");
    gtk_box_pack_end(GTK_BOX(details_top), cancel_selected_button, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(details_top), view_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(details_box), details_top, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(page), details_card, FALSE, FALSE, 0);

    g_signal_connect(search_button, "clicked", G_CALLBACK(on_search), NULL);
    g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_cancel), NULL);
    g_signal_connect(entry_search, "activate", G_CALLBACK(on_search), NULL);
    g_signal_connect(entry_cancel, "activate", G_CALLBACK(on_cancel), NULL);
    g_signal_connect(combo_filter_class, "changed", G_CALLBACK(on_filter_changed), NULL);
    g_signal_connect(combo_filter_status, "changed", G_CALLBACK(on_filter_changed), NULL);
    g_signal_connect(view_button, "clicked", G_CALLBACK(on_view_selected), NULL);
    g_signal_connect(cancel_selected_button, "clicked", G_CALLBACK(on_cancel_selected), NULL);

    gtk_stack_add_titled(GTK_STACK(stack), page, "passengers", "🚆 Passengers");
}

static void build_activity_page(GtkWidget *stack)
{
    GtkWidget *page = make_page();
    GtkWidget *header;
    GtkWidget *log_card;
    GtkWidget *box;
    GtkWidget *head;
    GtkWidget *clear_button;
    GtkWidget *scroll;
    GtkWidget *bottom;
    GtkWidget *new_button;
    GtkWidget *reset_button;
    GtkWidget *engine_card;

    header = make_section_header("System Activity",
                                 "Live audit trail of bookings, cancellations, queue promotions and system actions.");
    gtk_box_pack_start(GTK_BOX(page), header, FALSE, FALSE, 0);

    log_card = make_card();
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_add(GTK_CONTAINER(log_card), box);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);

    head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    lbl_status = make_label("System ready.", "activity-title");
    clear_button = gtk_button_new_with_label("Clear Log");
    gtk_style_context_add_class(gtk_widget_get_style_context(clear_button), "soft");
    gtk_box_pack_start(GTK_BOX(head), lbl_status, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(head), clear_button, FALSE, FALSE, 0);

    log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(log_view), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(log_view), "activity-view");
    log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_size_request(scroll, -1, 390);
    gtk_container_add(GTK_CONTAINER(scroll), log_view);
    gtk_box_pack_start(GTK_BOX(box), head, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), log_card, TRUE, TRUE, 0);

    engine_card = make_card();
    {
        GtkWidget *ec = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_container_add(GTK_CONTAINER(engine_card), ec);
        gtk_container_set_border_width(GTK_CONTAINER(ec), 10);
        gtk_box_pack_start(GTK_BOX(ec), make_badge("FIFO", "badge-confirmed"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(ec), make_label("Queue engine is active. Confirmed seats are allocated automatically; cancellations trigger same-class FIFO promotion.", "mini"), TRUE, TRUE, 0);
    }
    gtk_box_pack_start(GTK_BOX(page), engine_card, FALSE, FALSE, 0);

    bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    new_button = gtk_button_new_with_label("＋ New Journey");
    reset_button = gtk_button_new_with_label("↻ Reset System");
    gtk_style_context_add_class(gtk_widget_get_style_context(new_button), "soft");
    gtk_style_context_add_class(gtk_widget_get_style_context(reset_button), "danger");
    gtk_box_pack_end(GTK_BOX(bottom), reset_button, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(bottom), new_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), bottom, FALSE, FALSE, 0);

    g_signal_connect(clear_button, "clicked", G_CALLBACK(on_clear_log), NULL);
    g_signal_connect(new_button, "clicked", G_CALLBACK(on_new_journey), NULL);
    g_signal_connect(reset_button, "clicked", G_CALLBACK(on_reset), NULL);

    gtk_stack_add_titled(GTK_STACK(stack), page, "activity", "🚆 Activity Log");
}

static void build_settings_page(GtkWidget *stack)
{
    GtkWidget *page = make_page();
    GtkWidget *header;
    GtkWidget *row;
    GtkWidget *train_card;
    GtkWidget *prefs_card;
    GtkWidget *about_card;

    header = make_section_header("Settings & System Info",
                                 "Application preferences and the configured educational train profile.");
    gtk_box_pack_start(GTK_BOX(page), header, FALSE, FALSE, 0);

    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_set_homogeneous(GTK_BOX(row), TRUE);

    train_card = make_card();
    {
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_container_add(GTK_CONTAINER(train_card), box);
        gtk_container_set_border_width(GTK_CONTAINER(box), 14);
        gtk_box_pack_start(GTK_BOX(box), make_label("TRAIN PROFILE", "section-title"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), make_label("Kerala Express", "setting-title"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), make_label("Train No. 12625", "mini"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), make_label("Thiruvananthapuram → New Delhi", "mini"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), make_label("Departure 12:30 PM • Arrival 06:20 AM", "mini"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), make_label("Capacity: 38 confirmed + 10 waiting", "mini"), FALSE, FALSE, 0);
    }

    prefs_card = make_card();
    {
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        GtkWidget *line;
        gtk_container_add(GTK_CONTAINER(prefs_card), box);
        gtk_container_set_border_width(GTK_CONTAINER(box), 14);
        gtk_box_pack_start(GTK_BOX(box), make_label("PREFERENCES", "section-title"), FALSE, FALSE, 0);

        line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_pack_start(GTK_BOX(line), make_label("Dark Mode", "setting-title"), TRUE, TRUE, 0);
        switch_dark = gtk_switch_new();
        gtk_switch_set_active(GTK_SWITCH(switch_dark), dark_mode);
        gtk_box_pack_end(GTK_BOX(line), switch_dark, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), line, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), make_label("Switch between the polished light and dark dashboard themes.", "setting-desc"), FALSE, FALSE, 0);

        line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_pack_start(GTK_BOX(line), make_label("Confirmation Dialogs", "setting-title"), TRUE, TRUE, 0);
        switch_confirm = gtk_switch_new();
        gtk_switch_set_active(GTK_SWITCH(switch_confirm), confirm_actions);
        gtk_box_pack_end(GTK_BOX(line), switch_confirm, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), line, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), make_label("Ask before cancellations, resets and new journeys.", "setting-desc"), FALSE, FALSE, 0);
    }

    gtk_box_pack_start(GTK_BOX(row), train_card, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row), prefs_card, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), row, FALSE, FALSE, 0);

    about_card = make_card();
    {
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_container_add(GTK_CONTAINER(about_card), box);
        gtk_container_set_border_width(GTK_CONTAINER(box), 14);
        gtk_box_pack_start(GTK_BOX(box), make_label("ABOUT THIS MASTERPIECE", "section-title"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), make_label("Railway Reservation System • Master V4", "setting-title"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), make_label("C language + GTK3 + GLib. Designed around linked-list FIFO queues, automatic seat allocation, searchable passenger records, live statistics and an educational e-ticket workflow.", "setting-desc"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), make_label("No external GUI toolkit is required beyond GTK3, keeping the build simple on Ubuntu.", "setting-desc"), FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(page), about_card, FALSE, FALSE, 0);

    g_signal_connect(switch_dark, "notify::active", G_CALLBACK(on_dark_mode_changed), NULL);
    g_signal_connect(switch_confirm, "notify::active", G_CALLBACK(on_confirm_setting_changed), NULL);

    gtk_stack_add_titled(GTK_STACK(stack), page, "settings", "⚙ Settings");
}

/* ---------- Main window and main() ---------- */

static void build_main_window(GtkApplication *app)
{
    GtkWidget *root;
    GtkWidget *content;
    GtkWidget *sidebar;
    GtkWidget *header;
    GtkWidget *header_right;
    GtkWidget *stack_scroll;

    load_css();

    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Railway Reservation System — Master V4");
    gtk_window_set_default_size(GTK_WINDOW(window), 1120, 700);
    gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

    header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "Railway Reservation");
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header), "Kerala Express • 12625 • Thiruvananthapuram → New Delhi");

    header_right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(header_right), "header-box");
    header_status = make_label("● SYSTEM ONLINE", "online");
    header_clock = make_label("--:--:--", "header-clock");
    gtk_box_pack_start(GTK_BOX(header_right), header_status, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header_right), header_clock, FALSE, FALSE, 0);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), header_right);
    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), root);

    notification_bar = gtk_info_bar_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(notification_bar), "infobar");
    {
        GtkWidget *area = gtk_info_bar_get_content_area(GTK_INFO_BAR(notification_bar));
        notification_label = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(notification_label), 0.0f);
        gtk_box_pack_start(GTK_BOX(area), notification_label, TRUE, TRUE, 0);
    }
    gtk_box_pack_start(GTK_BOX(root), notification_bar, FALSE, FALSE, 0);

    content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_vexpand(content, TRUE);
    gtk_box_pack_start(GTK_BOX(root), content, TRUE, TRUE, 0);

    main_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(main_stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_set_transition_duration(GTK_STACK(main_stack), 180);
    gtk_widget_set_hexpand(main_stack, TRUE);
    gtk_widget_set_vexpand(main_stack, TRUE);

    stack_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(stack_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(stack_scroll), TRUE);
    gtk_widget_set_hexpand(stack_scroll, TRUE);
    gtk_widget_set_vexpand(stack_scroll, TRUE);
    gtk_container_add(GTK_CONTAINER(stack_scroll), main_stack);

    sidebar = gtk_stack_sidebar_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(sidebar), "sidebar");
    gtk_widget_set_size_request(sidebar, 175, -1);
    gtk_stack_sidebar_set_stack(GTK_STACK_SIDEBAR(sidebar), GTK_STACK(main_stack));

    gtk_box_pack_start(GTK_BOX(content), sidebar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), stack_scroll, TRUE, TRUE, 0);

    build_dashboard_page(main_stack);
    build_booking_page(main_stack);
    build_passenger_page(main_stack);
    build_activity_page(main_stack);
    build_settings_page(main_stack);

    gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "dashboard");

    gtk_widget_show_all(window);
    /* gtk_widget_show_all() re-shows the hidden InfoBar, so hide it after the full tree is shown. */
    gtk_widget_hide(notification_bar);

    update_clock(NULL);
    g_timeout_add_seconds(1, update_clock, NULL);
    refresh_tables();
    update_booking_availability();
    log_msg("SYSTEM READY: Railway Reservation Master V4 started.");
    log_msg("FIFO queue engine active. Dashboard, booking, passenger and activity modules loaded.");
    log_msg("Seat capacity: SL20 / 3A10 / 2A5 / 1A3. Waiting limit: 10.");
}

int main(int argc, char **argv)
{
    GtkApplication *app;
    int status;

    queue_init(&confirmed);
    queue_init(&waiting);

    app = gtk_application_new("edu.railway.reservation.master.v4",
                              G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(build_main_window), NULL);

    status = g_application_run(G_APPLICATION(app), argc, argv);

    if (notification_source != 0)
        g_source_remove(notification_source);

    g_object_unref(app);
    free_queue(&confirmed);
    free_queue(&waiting);
    return status;
}
