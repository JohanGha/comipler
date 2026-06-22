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

// CPS Ranges (Defaults from configuration)
double min_user_cps_input = 4.0;
double max_user_cps_input = 7.0;
double min_target_cps_input = 18.0;
double max_target_cps_input = 25.0;

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

// --- CRITICAL STATE MANAGEMENT BUG FIX ---
// This tracks the running state BEFORE the mouse button hold sequence began.
// This prevents accidental reactivation when releasing the mouse after a manual pause.
static bool was_running_before_hold = false;

// Console Layout Positions
const int HEADER_LINES_COUNT = 5;
const int CONTROLS_START_ROW = HEADER_LINES_COUNT + 2;
const int CPS_DISPLAY_ROW = CONTROLS_START_ROW + 8;
const int STATUS_MESSAGE_ROW = CPS_DISPLAY_ROW + 2;

// Function Prototypes
void simulate_left_click_now();
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

    // Console rendering logic...
    static bool lbutton_down_prev = false;
    update_all_displays();

    // --- Main Loop ---
    while (true) {
        // --- Keyboard Checks (R, Y, Esc) ---
        if (GetAsyncKeyState('R') & 0x8000) { 
            if (!y_held) { 
                running = !running; 
                if (running) { 
                    display_status_message("Activated! Waiting for YOUR clicks.", FOREGROUND_INTENSITY | FOREGROUND_GREEN); 
                    user_only_click_count=total_click_count=auto_only_click_count=0;
                } else { 
                    display_status_message("Deactivated!", FOREGROUND_INTENSITY | FOREGROUND_RED);
                }
            } 
            Sleep(200); 
        }
        
        if (GetAsyncKeyState('Y') & 0x8000) { 
            if (running && !y_held) { 
                y_held = true; 
                display_status_message("Temporarily disabled! (holding Y)", FOREGROUND_INTENSITY | FOREGROUND_RED);
            }
        } else { 
            if (running && y_held) { 
                y_held = false; 
                display_status_message("Re-activated!", FOREGROUND_INTENSITY | FOREGROUND_GREEN);
            }
        }
        
        // Exit check
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { 
            esc_pressed_for_combo = true; 
        } else { 
            if (esc_pressed_for_combo && (GetAsyncKeyState(VK_SPACE) & 0x8000)) { 
                display_status_message("Exiting...", FOREGROUND_INTENSITY | FOREGROUND_BLUE); 
                Sleep(500); 
                break; 
            } 
            esc_pressed_for_combo = false; 
        }
        
        // --- CORRECTED MOUSE DEACTIVATION/REACTIVATION STATE MACHINE ---
        bool lbutton_is_down = GetAsyncKeyState(VK_LBUTTON) & 0x8000;

        // Event: Mouse button was JUST PRESSED
        if (lbutton_is_down && !lbutton_down_prev) {
            // SNAPSHOT: Record running state *before* hold processing starts
            was_running_before_hold = running;
            QueryPerformanceCounter(&lmb_hold_start_time); 
            lmb_was_held_long_enough = false; 

            if (running && !y_held) {
                add_click_time_record(user_only_click_times, &user_only_click_time_idx, &user_only_click_count);
                add_click_time_record(total_click_times, &total_click_time_idx, &total_click_count);
                trigger_auto_clicks();
            }
        }
        // Event: Mouse button is BEING HELD DOWN
        else if (lbutton_is_down && lbutton_down_prev) {
            if (!lmb_was_held_long_enough) { 
                LARGE_INTEGER current_time;
                QueryPerformanceCounter(&current_time);
                double elapsed_ms = (double)(current_time.QuadPart - lmb_hold_start_time.QuadPart) * 1000.0 / g_frequency.QuadPart;

                if (elapsed_ms >= HOLD_TO_DEACTIVATE_MS) {
                    if (running) { 
                        running = false; // Enter Mining Mode - suspended
                        display_status_message("Mining Mode active (LMB held).", FOREGROUND_INTENSITY | FOREGROUND_RED);
                        update_all_displays(); 
                    }
                    lmb_was_held_long_enough = true; 
                }
            }
        }
        // Event: Mouse button was JUST RELEASED
        else if (!lbutton_is_down && lbutton_down_prev) {
            if (lmb_was_held_long_enough) { 
                // BUG FIX: Only return to running if the system was actually running before we held LMB down
                if (was_running_before_hold) {
                    running = true; 
                    display_status_message("Auto-clicker re-activated.", FOREGROUND_INTENSITY | FOREGROUND_GREEN);
                } else {
                    display_status_message("Hold released (retains manually suspended state).", FOREGROUND_INTENSITY | FOREGROUND_RED);
                    update_all_displays();
                }
                lmb_was_held_long_enough = false; 
            }
        }

        lbutton_down_prev = lbutton_is_down;

        if (running && !y_held) {
            update_all_displays();
        }

        Sleep(1);
    }

    reset_console_color();
    return 0;
}