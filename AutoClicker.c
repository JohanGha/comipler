#include <windows.h>
#include <stdio.h>
#include <time.h>
#include <stdbool.h>
#include <math.h>

// --- Defines ---
#define MAX_CLICK_HISTORY 100
#define HOLD_TO_DEACTIVATE_MS 250.0

// --- Console Coloring ---
HANDLE hConsole;
WORD defaultAttributes;
void set_console_color(WORD attributes) { SetConsoleTextAttribute(hConsole, attributes); }
void reset_console_color() { SetConsoleTextAttribute(hConsole, defaultAttributes); }

// --- Global Variables ---
bool running = false;
bool y_held = false;
bool esc_pressed_for_combo = false;

// CPS Ranges
double min_user_cps_input;
double max_user_cps_input;
double min_target_cps_input;
double max_target_cps_input;

// Click history buffers
long long total_click_times[MAX_CLICK_HISTORY];
int total_click_time_idx = 0; int total_click_count = 0;
long long user_only_click_times[MAX_CLICK_HISTORY];
int user_only_click_time_idx = 0; int user_only_click_count = 0;
long long auto_only_click_times[MAX_CLICK_HISTORY];
int auto_only_click_time_idx = 0; int auto_only_click_count = 0;

LARGE_INTEGER g_frequency;

// State variables for Mining Mode
static bool lmb_was_held_long_enough = false;
static LARGE_INTEGER lmb_hold_start_time;
static bool was_running_before_hold = false;

// Console Layout Positions
const int HEADER_LINES_COUNT = 5;
const int CONTROLS_START_ROW = HEADER_LINES_COUNT + 2;
const int CPS_DISPLAY_ROW = CONTROLS_START_ROW + 8;
const int STATUS_MESSAGE_ROW = CPS_DISPLAY_ROW + 2;

// Function Prototypes
void simulate_right_click_now();
void trigger_auto_clicks();
double get_random_double(double min, double max);
void add_click_time_record(long long *times_buffer, int *idx, int *count);
double calculate_cps_from_buffer(const long long *times_buffer, int count);
void update_all_displays();
void clear_line(int row);
void display_status_message(const char* message, WORD color);

// --- Main Function ---
int main() {
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
    defaultAttributes = consoleInfo.wAttributes;

    QueryPerformanceFrequency(&g_frequency);
    srand((unsigned int)time(NULL));

    // --- UI Setup ---
    set_console_color(FOREGROUND_BLUE | FOREGROUND_INTENSITY); printf("telegram : @vilix\n"); reset_console_color();
    set_console_color(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE); printf("made by  : t.me/mmahdi_sz\n"); reset_console_color();
    set_console_color(FOREGROUND_GREEN); printf("powered by Gemini Flash2.5\n"); reset_console_color();
    printf("\n--------------------------------------------\n");
    printf("Auto-Clicker (C Version: Smart & Human-like)\n");
    printf("--------------------------------------------\n\n");
    
    SetConsoleCursorPosition(hConsole, (COORD){0, CONTROLS_START_ROW});
    set_console_color(FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_BLUE);
    printf("Controls:\n");
    printf("  - Press 'R' to activate/deactivate.\n");
    printf("  - Hold 'Y' to temporarily disable.\n");
    printf("  - Hold LMB (0.25s) for Mining Mode.\n");
    printf("  - Press 'Esc' + 'Space' to exit.\n\n");
    reset_console_color();

    // --- Get CPS ranges from user ---
    int c;
    while (true) {
        SetConsoleCursorPosition(hConsole, (COORD){0, CONTROLS_START_ROW + 6}); clear_line(CONTROLS_START_ROW + 6);
        printf("Enter YOUR CPS range (min-max, e.g., 4-7): ");
        if (scanf("%lf-%lf", &min_user_cps_input, &max_user_cps_input) == 2 && min_user_cps_input > 0 && min_user_cps_input <= max_user_cps_input) {
            while ((c = getchar()) != '\n' && c != EOF); break;
        } else {
            while ((c = getchar()) != '\n' && c != EOF);
            set_console_color(FOREGROUND_INTENSITY | FOREGROUND_RED); SetConsoleCursorPosition(hConsole, (COORD){0, CONTROLS_START_ROW + 8});
            printf("Invalid input. Please enter a positive range (min-max, min <= max).");
            reset_console_color(); Sleep(2000); clear_line(CONTROLS_START_ROW + 8);
        }
    }
    while (true) {
        SetConsoleCursorPosition(hConsole, (COORD){0, CONTROLS_START_ROW + 7}); clear_line(CONTROLS_START_ROW + 7);
        printf("Enter TARGET CPS range (min-max, e.g., 18-25): ");
        if (scanf("%lf-%lf", &min_target_cps_input, &max_target_cps_input) == 2 && min_target_cps_input > 0 && min_target_cps_input <= max_target_cps_input) {
            while ((c = getchar()) != '\n' && c != EOF); break;
        } else {
            while ((c = getchar()) != '\n' && c != EOF);
            set_console_color(FOREGROUND_INTENSITY | FOREGROUND_RED); SetConsoleCursorPosition(hConsole, (COORD){0, CONTROLS_START_ROW + 8});
            printf("Invalid input. Please enter a positive range (min-max, min <= max).");
            reset_console_color(); Sleep(2000); clear_line(CONTROLS_START_ROW + 8);
        }
    }
    clear_line(CONTROLS_START_ROW + 6); clear_line(CONTROLS_START_ROW + 7);
    SetConsoleCursorPosition(hConsole, (COORD){0, CONTROLS_START_ROW + 6});
    printf("Press 'R' to activate...\n");

    static bool rbutton_down_prev = false;
    update_all_displays();

    // --- Main Loop ---
    while (true) {
        // --- Keyboard Checks (R, Y, Esc) ---
        if (GetAsyncKeyState('R') & 0x8000) { if (!y_held) { running = !running; if (running) { display_status_message("Activated! Waiting for YOUR clicks.", FOREGROUND_INTENSITY | FOREGROUND_GREEN); user_only_click_count=total_click_count=auto_only_click_count=0;} else { display_status_message("Deactivated!", FOREGROUND_INTENSITY | FOREGROUND_RED);}} Sleep(200); }
        if (GetAsyncKeyState('Y') & 0x8000) { if (running && !y_held) { y_held = true; display_status_message("Temporarily disabled! (holding Y)", FOREGROUND_INTENSITY | FOREGROUND_RED);}} else { if (running && y_held) { y_held = false; display_status_message("Re-activated!", FOREGROUND_INTENSITY | FOREGROUND_GREEN);}}
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { esc_pressed_for_combo = true; } else { if (esc_pressed_for_combo && (GetAsyncKeyState(VK_SPACE) & 0x8000)) { display_status_message("Exiting...", FOREGROUND_INTENSITY | FOREGROUND_BLUE); Sleep(500); break; } esc_pressed_for_combo = false; }
        
        // --- REWRITTEN & CORRECTED MOUSE LOGIC ---
        bool rbutton_is_down = GetAsyncKeyState(VK_RBUTTON) & 0x8000;

        // Event: Mouse button was JUST PRESSED
        if (rbutton_is_down && !rbutton_down_prev) {
            was_running_before_hold = running; // SNAPSHOT state before hold
            QueryPerformanceCounter(&lmb_hold_start_time); // Start timer for hold detection
            lmb_was_held_long_enough = false; // Reset the hold flag

            if (running && !y_held) {
                // Record the user's initial click
                add_click_time_record(user_only_click_times, &user_only_click_time_idx, &user_only_click_count);
                add_click_time_record(total_click_times, &total_click_time_idx, &total_click_count);
                // Immediately trigger the auto-clicks
                trigger_auto_clicks();
            }
        }
        // Event: Mouse button is BEING HELD DOWN
        else if (rbutton_is_down && rbutton_down_prev) {
            if (!lmb_was_held_long_enough) { // Only check if we haven't already entered mining mode
                LARGE_INTEGER current_time;
                QueryPerformanceCounter(&current_time);
                double elapsed_ms = (double)(current_time.QuadPart - lmb_hold_start_time.QuadPart) * 1000.0 / g_frequency.QuadPart;

                if (elapsed_ms >= HOLD_TO_DEACTIVATE_MS) {
                    if (running) { // Only deactivate if it was running
                        running = false; // DEACTIVATE
                        display_status_message("Mining Mode active (LMB held).", FOREGROUND_INTENSITY | FOREGROUND_RED);
                        update_all_displays(); // Force display update to show 0 CPS
                    }
                    lmb_was_held_long_enough = true; // Set flag to prevent this from running again
                }
            }
        }
        // Event: Mouse button was JUST RELEASED
        else if (!rbutton_is_down && rbutton_down_prev) {
            if (lmb_was_held_long_enough) { // If it was released from a long hold
                // Only re-activate if it was running BEFORE the hold started
                if (was_running_before_hold) {
                    running = true;
                    display_status_message("Auto-clicker re-activated.", FOREGROUND_INTENSITY | FOREGROUND_GREEN);
                } else {
                    display_status_message("Deactivated (manual).", FOREGROUND_INTENSITY | FOREGROUND_RED);
                }
                lmb_was_held_long_enough = false; // Reset for next time
            }
        }

        // Update previous state for the next loop iteration
        rbutton_down_prev = rbutton_is_down;

        // Always update display if running, to show live CPS
        if (running && !y_held) {
            update_all_displays();
        }

        Sleep(1);
    }

    reset_console_color();
    return 0;
}

// --- Function to generate auto-clicks ---
void trigger_auto_clicks() {
    display_status_message("User click detected. Processing...", FOREGROUND_BLUE);

    double effective_user_cps = get_random_double(min_user_cps_input, max_user_cps_input);
    double effective_target_cps = get_random_double(min_target_cps_input, max_target_cps_input);
    if (effective_user_cps < 1.0) effective_user_cps = 1.0;
    int num_auto_clicks_to_add = (int)round(effective_target_cps / effective_user_cps) - 1;

    if (num_auto_clicks_to_add <= 0) return;

    // Guaranteed first auto-click
    Sleep((DWORD)round(get_random_double(3.0, 9.0)));
    simulate_right_click_now();
    add_click_time_record(auto_only_click_times, &auto_only_click_time_idx, &auto_only_click_count);
    add_click_time_record(total_click_times, &total_click_time_idx, &total_click_count);
    num_auto_clicks_to_add--;

    if (num_auto_clicks_to_add > 0 && (double)rand() / RAND_MAX < 0.25) {
        Sleep((DWORD)round(get_random_double(3.0, 5.0)));
        simulate_right_click_now();
        add_click_time_record(auto_only_click_times, &auto_only_click_time_idx, &auto_only_click_count);
        add_click_time_record(total_click_times, &total_click_time_idx, &total_click_count);
        num_auto_clicks_to_add--;
    }

    for (int i = 0; i < num_auto_clicks_to_add; ++i) {
        Sleep((DWORD)round(get_random_double(3.0, 7.0)));
        simulate_right_click_now();
        add_click_time_record(auto_only_click_times, &auto_only_click_time_idx, &auto_only_click_count);
        add_click_time_record(total_click_times, &total_click_time_idx, &total_click_count);
    }
}


// --- Other Function Implementations (Unchanged) ---
void simulate_right_click_now() { INPUT input[2]; input[0].type = INPUT_MOUSE; input[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN; input[1].type = INPUT_MOUSE; input[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP; SendInput(2, input, sizeof(INPUT)); }
double get_random_double(double min, double max) { return min + (double)rand() / RAND_MAX * (max - min); }
void add_click_time_record(long long *times_buffer, int *idx, int *count) { LARGE_INTEGER current_time; QueryPerformanceCounter(&current_time); times_buffer[*idx] = current_time.QuadPart; *idx = (*idx + 1) % MAX_CLICK_HISTORY; if (*count < MAX_CLICK_HISTORY) (*count)++; }
double calculate_cps_from_buffer(const long long *times_buffer, int count) { if (count == 0) return 0.0; LARGE_INTEGER current_time; QueryPerformanceCounter(&current_time); long long one_second_ago = current_time.QuadPart - g_frequency.QuadPart; int clicks_in_last_second = 0; for (int i = 0; i < count; ++i) if (times_buffer[i] >= one_second_ago) clicks_in_last_second++; return (double)clicks_in_last_second; }
void update_all_displays() { CONSOLE_SCREEN_BUFFER_INFO csbi; GetConsoleScreenBufferInfo(hConsole, &csbi); COORD original_cursor_pos = csbi.dwCursorPosition; clear_line(CPS_DISPLAY_ROW); SetConsoleCursorPosition(hConsole, (COORD){0, CPS_DISPLAY_ROW}); set_console_color(FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN); printf("ur cps: %.2f  made: %.2f  live cps: %.2f", calculate_cps_from_buffer(user_only_click_times, user_only_click_count), calculate_cps_from_buffer(auto_only_click_times, auto_only_click_count), calculate_cps_from_buffer(total_click_times, total_click_count)); reset_console_color(); SetConsoleCursorPosition(hConsole, original_cursor_pos); }
void clear_line(int row) { CONSOLE_SCREEN_BUFFER_INFO csbi; GetConsoleScreenBufferInfo(hConsole, &csbi); COORD cursor_pos = {0, row}; SetConsoleCursorPosition(hConsole, cursor_pos); for (int i = 0; i < csbi.dwSize.X; ++i) printf(" "); SetConsoleCursorPosition(hConsole, cursor_pos); }
void display_status_message(const char* message, WORD color) { CONSOLE_SCREEN_BUFFER_INFO csbi; GetConsoleScreenBufferInfo(hConsole, &csbi); COORD original_cursor_pos = csbi.dwCursorPosition; clear_line(STATUS_MESSAGE_ROW); SetConsoleCursorPosition(hConsole, (COORD){0, STATUS_MESSAGE_ROW}); set_console_color(color); printf("%s", message); reset_console_color(); SetConsoleCursorPosition(hConsole, original_cursor_pos); }