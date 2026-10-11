#pragma once

// Registry of test entry points. Add one declaration per new test file, main.cpp calls each in
// turn.
void run_bit_tests();
void run_canvas_tests();
void run_diff_tests();
void run_host_tests();
void run_pen_tests();
void run_point_tests();
void run_rect_tests();
void run_smoke_tests();
