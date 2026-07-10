#include <iostream>
#include <string>
#include <vector>
#include <regex>
#include <chrono>
#include <thread>
#include <filesystem>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <clocale>
#include <csignal>
#include <atomic>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

// 跨平台 popen/pclose
#ifdef _WIN32
    #define POPEN _popen
    #define PCLOSE _pclose
#else
    #define POPEN popen
    #define PCLOSE pclose
#endif

std::atomic<bool> g_running(true);

// 信号处理函数（Ctrl+C）
void signal_handler(int sig) {
    if (sig == SIGINT) {
        g_running = false;
        std::cout << "\n收到中断信号，正在退出..." << std::endl;
#ifdef _WIN32
        system("taskkill /F /IM ffmpeg.exe >nul 2>&1");
        system("taskkill /F /IM ffprobe.exe >nul 2>&1");
#else
        system("pkill -9 ffmpeg");
        system("pkill -9 ffprobe");
#endif
    }
}

// ================== 路径转换函数（Windows 短路径） ==================
std::string to_short_path(const std::string& path) {
#ifdef _WIN32
    // 先转为宽字符（使用当前代码页，即 GBK）
    int wlen = MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, nullptr, 0);
    if (wlen == 0) return path;
    std::wstring wpath(wlen, L'\0');
    MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, &wpath[0], wlen);
    // 去除末尾多余的空字符
    wpath.resize(wpath.size() - 1);

    // 获取短路径长度
    wlen = GetShortPathNameW(wpath.c_str(), nullptr, 0);
    if (wlen == 0) return path;  // 转换失败，返回原路径
    std::wstring shortPath(wlen, L'\0');
    GetShortPathNameW(wpath.c_str(), &shortPath[0], wlen);
    shortPath.resize(shortPath.size() - 1);

    // 转回 ANSI（GBK）
    int len = WideCharToMultiByte(CP_ACP, 0, shortPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len == 0) return path;
    std::string result(len, '\0');
    WideCharToMultiByte(CP_ACP, 0, shortPath.c_str(), -1, &result[0], len, nullptr, nullptr);
    result.resize(result.size() - 1);
    return result;
#else
    return path;
#endif
}
// 使用 Windows API 检查文件是否存在（支持中文路径）
bool file_exists(const std::string& path) {
#ifdef _WIN32
    // 将 GBK 路径转为 UTF-16 宽字符串
    int wlen = MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, nullptr, 0);
    if (wlen == 0) return false;
    std::wstring wpath(wlen, L'\0');
    MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, &wpath[0], wlen);
    wpath.resize(wpath.size() - 1);  // 去掉末尾空字符

    DWORD attr = GetFileAttributesW(wpath.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
#else
    return fs::exists(path);
#endif
}

// 为命令行添加双引号并转换路径
std::string quote_short_path(const std::string& path) {
    return "\"" + to_short_path(path) + "\"";
}

// ------------------ 辅助函数 ------------------
void clear_screen() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

std::string get_input(const std::string& prompt,
                       const std::string& default_val = "",
                       bool is_path = false,
                       bool is_output = false,
                       const std::string& default_ext = "") {
    while (g_running) {
        std::cout << prompt;
        std::string input;
        std::getline(std::cin, input);
        if (!g_running) return "";
        // 去除首尾空白
        input.erase(0, input.find_first_not_of(" \t\n\r"));
        input.erase(input.find_last_not_of(" \t\n\r") + 1);

        // 去除首尾双引号
        if (input.size() >= 2 && input.front() == '"' && input.back() == '"') {
            input = input.substr(1, input.size() - 2);
        }

        if (!input.empty()) {
            if (is_path && !file_exists(input)) {   // 这里改为 file_exists
                std::cout << "文件不存在: " << input << std::endl;
                continue;
            }
            if (is_output && !default_ext.empty() && input.find('.') == std::string::npos) {
                input += default_ext;
            }
            return input;
        } else if (!default_val.empty()) {
            std::string result = default_val;
            if (is_output && !default_ext.empty() && result.find('.') == std::string::npos) {
                result += default_ext;
            }
            return result;
        }
        std::cout << "输入不能为空，请重新输入" << std::endl;
    }
    return "";
}

bool run_ffmpeg(const std::string& cmd) {
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        std::cout << "执行出错，返回码: " << ret << std::endl;
        return false;
    }
    return true;
}

double parse_duration(const std::string& time_str) {
    std::regex pattern(R"((\d+):(\d+):(\d+\.\d+))");
    std::smatch matches;
    if (std::regex_search(time_str, matches, pattern)) {
        double hours = std::stod(matches[1]);
        double minutes = std::stod(matches[2]);
        double seconds = std::stod(matches[3]);
        return hours * 3600 + minutes * 60 + seconds;
    }
    return 0.0;
}

double get_video_duration(const std::string& input_file) {
    std::string cmd = "ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 " + quote_short_path(input_file);
    std::unique_ptr<FILE, decltype(&PCLOSE)> pipe(POPEN(cmd.c_str(), "r"), PCLOSE);
    if (!pipe) {
        return 0.0;
    }
    char buffer[128];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe.get()) != nullptr) {
        result += buffer;
    }
    try {
        return std::stod(result);
    } catch (...) {
        return 0.0;
    }
}

std::string format_file_size(size_t size_bytes) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int i = 0;
    double size = static_cast<double>(size_bytes);
    while (size >= 1024 && i < 3) {
        size /= 1024;
        ++i;
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << size << " " << units[i];
    return oss.str();
}

// 带进度条的执行函数（支持中断）
bool run_ffmpeg_with_progress(const std::string& cmd, double total_duration) {
    auto start_time = std::chrono::steady_clock::now();

    std::string full_cmd = cmd + " 2>&1";
    std::unique_ptr<FILE, decltype(&PCLOSE)> pipe(POPEN(full_cmd.c_str(), "r"), PCLOSE);
    if (!pipe) {
        std::cout << "无法执行命令" << std::endl;
        return false;
    }

    std::regex time_pattern(R"(time=(\d+:\d+:\d+\.\d+))");
    std::regex speed_pattern(R"(speed=([\d.]+)x)");
    char buffer[256];
    std::cout << "\n转换进度：" << std::endl;
    std::cout << "[0%] 正在初始化..." << std::flush;

    bool success = false;
    while (fgets(buffer, sizeof(buffer), pipe.get()) != nullptr && g_running) {
        std::string line(buffer);
        std::smatch time_match;
        if (std::regex_search(line, time_match, time_pattern) && total_duration > 0) {
            double current_time = parse_duration(time_match[1]);
            double progress = std::min(current_time / total_duration, 1.0);

            auto elapsed = std::chrono::steady_clock::now() - start_time;
            double elapsed_sec = std::chrono::duration<double>(elapsed).count();

            double speed = 1.0;
            std::smatch speed_match;
            if (std::regex_search(line, speed_match, speed_pattern)) {
                speed = std::stod(speed_match[1]);
            }
            double remaining = (total_duration - current_time) / (speed > 0 ? speed : 1.0);

            int bar_len = 20;
            int filled = static_cast<int>(progress * bar_len);
            std::string bar(filled, '#');
            bar.append(bar_len - filled, '-');

            std::ostringstream time_info;
            time_info << "已用: " << std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() << "s | "
                      << "剩余: " << static_cast<int>(remaining) << "s | "
                      << "速度: " << std::fixed << std::setprecision(1) << speed << "x";

            std::cout << "\r[" << std::fixed << std::setprecision(1) << progress * 100 << "%] " << bar << " | " << time_info.str() << std::flush;
        }
    }

    int ret = PCLOSE(pipe.release());
    success = (ret == 0) && g_running;
    if (!g_running) {
        std::cout << "\n操作被用户中断" << std::endl;
    } else {
        std::cout << "\n\n" << (success ? "转换完成！" : "转换失败！") << std::endl;
    }
    return success;
}

// ------------------ 功能函数 ------------------
void show_menu() {
    clear_screen();
    std::cout << "    FFmpeg工具箱 - 请选择操作：" << std::endl;
    std::cout << "    1. 显示媒体详细信息" << std::endl;
    std::cout << "    2. 提取封面图片" << std::endl;
    std::cout << "    3. 复制音频流" << std::endl;
    std::cout << "    4. 复制视频流" << std::endl;
    std::cout << "    5. 删除元数据章节" << std::endl;
    std::cout << "    6. 添加封面图片" << std::endl;
    std::cout << "    7. 合并视频和音频流" << std::endl;
    std::cout << "    8. 生成多尺寸图标（用于ICO）" << std::endl;
    std::cout << "    9. 调整图片尺寸和质量" << std::endl;
    std::cout << "    10. 退出" << std::endl;
    std::cout << std::endl;
}

void show_info() {
    std::string input_file = get_input("请输入媒体文件路径: ", "", true);
    if (!g_running) return;
    std::string cmd = "ffmpeg -i " + quote_short_path(input_file);
    std::cout << "正在获取媒体信息..." << std::endl;
    std::system(cmd.c_str());
    std::cout << "\n按回车键继续...";
    std::cin.get();
}

void extract_cover() {
    std::string input_file = get_input("请输入视频文件路径: ", "", true);
    if (!g_running) return;
    std::string stream = get_input("输入封面流索引 [默认 0:2]: ", "0:2");
    std::string output = get_input("输出图片名称 [默认 cover]: ", "cover", false, true, ".png");
    if (!g_running) return;

    double total_duration = get_video_duration(input_file);
    std::string cmd = "ffmpeg -hide_banner -i " + quote_short_path(input_file) +
                      " -map " + stream +
                      " -c copy -progress pipe:1 -y " + quote_short_path(output);
    std::cout << "正在提取封面..." << std::endl;
    bool success = run_ffmpeg_with_progress(cmd, total_duration);

    if (success && fs::exists(output)) {
        std::cout << "封面提取成功: " << output << std::endl;
    } else {
        std::cout << "封面提取失败，请检查流索引" << std::endl;
    }
    std::cout << "\n按回车键继续...";
    std::cin.get();
}

void copy_audio() {
    std::string input_file = get_input("请输入视频文件路径: ", "", true);
    if (!g_running) return;
    std::string audio_stream = get_input("输入音频流索引 [默认 0:1]: ", "0:1");
    std::string output = get_input("输出音频文件名称 [默认 audio]: ", "audio", false, true, ".aac");
    if (!g_running) return;

    double total_duration = get_video_duration(input_file);
    std::string cmd = "ffmpeg -hide_banner -i " + quote_short_path(input_file) +
                      " -map " + audio_stream +
                      " -c copy -progress pipe:1 -y " + quote_short_path(output);
    std::cout << "正在提取音频..." << std::endl;
    bool success = run_ffmpeg_with_progress(cmd, total_duration);

    if (success && fs::exists(output)) {
        std::cout << "音频提取成功: " << output << std::endl;
    } else {
        std::cout << "音频提取失败，请检查流索引" << std::endl;
    }
    std::cout << "\n按回车键继续...";
    std::cin.get();
}

void copy_video() {
    std::string input_file = get_input("请输入视频文件路径: ", "", true);
    if (!g_running) return;
    std::string video_stream = get_input("输入视频流索引 [默认 0:0]: ", "0:0");
    std::string output = get_input("输出视频文件名称 [默认 video]: ", "video", false, true, ".mp4");
    if (!g_running) return;

    double total_duration = get_video_duration(input_file);
    std::string cmd = "ffmpeg -hide_banner -i " + quote_short_path(input_file) +
                      " -map " + video_stream +
                      " -c copy -progress pipe:1 -y " + quote_short_path(output);
    std::cout << "正在提取视频流..." << std::endl;
    bool success = run_ffmpeg_with_progress(cmd, total_duration);

    if (success && fs::exists(output)) {
        std::cout << "视频流提取成功: " << output << std::endl;
    } else {
        std::cout << "视频流提取失败，请检查流索引" << std::endl;
    }
    std::cout << "\n按回车键继续...";
    std::cin.get();
}

void remove_metadata() {
    std::string input_file = get_input("请输入源文件路径: ", "", true);
    if (!g_running) return;
    std::string video_streams_input = get_input("输入要保留的视频流（逗号分隔，默认 0:0）: ", "0:0");
    std::string audio_streams_input = get_input("输入要保留的音频流（逗号分隔，默认 0:1）: ", "0:1");
    std::string output = get_input("输出文件名 [默认 output]: ", "output", false, true, ".mp4");
    if (!g_running) return;

    std::vector<std::string> video_streams, audio_streams;
    std::stringstream vs(video_streams_input), as(audio_streams_input);
    std::string token;
    while (std::getline(vs, token, ',')) {
        token.erase(remove_if(token.begin(), token.end(), ::isspace), token.end());
        if (!token.empty()) video_streams.push_back(token);
    }
    while (std::getline(as, token, ',')) {
        token.erase(remove_if(token.begin(), token.end(), ::isspace), token.end());
        if (!token.empty()) audio_streams.push_back(token);
    }

    double total_duration = get_video_duration(input_file);
    std::string cmd = "ffmpeg -hide_banner -i " + quote_short_path(input_file) +
                      " -map_metadata -1 -map_chapters -1";
    for (const auto& s : video_streams) cmd += " -map " + s;
    for (const auto& s : audio_streams) cmd += " -map " + s;
    cmd += " -codec copy -progress pipe:1 -y " + quote_short_path(output);

    std::cout << "正在处理元数据..." << std::endl;
    bool success = run_ffmpeg_with_progress(cmd, total_duration);

    if (success) std::cout << "处理完成: " << output << std::endl;
    std::cout << "\n按回车键继续...";
    std::cin.get();
}

void add_cover() {
    std::string video = get_input("请输入视频文件路径: ", "", true);
    if (!g_running) return;
    std::string cover = get_input("请输入封面图片路径: ", "", true);
    std::string output = get_input("输出文件名 (默认 output): ", "output", false, true, ".mp4");
    if (!g_running) return;

    double total_duration = get_video_duration(video);
    std::string cmd = "ffmpeg -hide_banner -i " + quote_short_path(video) +
                      " -i " + quote_short_path(cover) +
                      " -map 0 -map 1 -c copy -disposition:v:1 attached_pic -progress pipe:1 -y " + quote_short_path(output);

    std::cout << "正在添加封面..." << std::endl;
    bool success = run_ffmpeg_with_progress(cmd, total_duration);

    if (success) std::cout << "封面添加成功: " << output << std::endl;
    std::cout << "\n按回车键继续...";
    std::cin.get();
}

void merge_av() {
    std::string video_file = get_input("请输入视频文件路径: ", "", true);
    if (!g_running) return;
    std::string audio_file = get_input("请输入音频文件路径: ", "", true);
    std::string output = get_input("输出文件名 (默认 merged): ", "merged", false, true, ".mp4");
    if (!g_running) return;

    double total_duration = get_video_duration(video_file);
    std::string cmd = "ffmpeg -hide_banner -i " + quote_short_path(video_file) +
                      " -i " + quote_short_path(audio_file) +
                      " -map 0:v:0 -map 1:a:0 -c copy -progress pipe:1 -y " + quote_short_path(output);

    std::cout << "正在合并视频和音频..." << std::endl;
    bool success = run_ffmpeg_with_progress(cmd, total_duration);

    if (success) {
        std::cout << "合并完成: " << output << std::endl;
    } else {
        std::cout << "合并失败，请检查输入文件" << std::endl;
    }
    std::cout << "\n按回车键继续...";
    std::cin.get();
}

void generate_icon_sizes() {
    std::string input_file = get_input("请输入源图片路径: ", "", true);
    if (!g_running) return;

    std::cout << "\n常用ICO尺寸: 16x16, 32x32, 48x48, 64x64, 128x128, 256x256" << std::endl;
    std::string sizes_input = get_input("请输入尺寸列表（用逗号分隔，例如: 256x256,128x128,64x64）: ");
    std::vector<std::string> sizes;
    std::stringstream ss(sizes_input);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token.erase(remove_if(token.begin(), token.end(), ::isspace), token.end());
        if (!token.empty()) sizes.push_back(token);
    }

    std::string prefix = get_input("请输入输出文件前缀 [默认 'icon']: ", "icon");
    if (!g_running) return;

    std::cout << "\n缩放模式选项:" << std::endl;
    std::cout << "1. 保持宽高比并填充为正方形（透明背景）" << std::endl;
    std::cout << "2. 保持宽高比并裁剪为正方形" << std::endl;
    std::cout << "3. 拉伸图像以适应目标尺寸（可能变形）" << std::endl;
    std::string scale_mode = get_input("请选择缩放模式 [默认 1]: ", "1");
    if (!g_running) return;

    int success_count = 0;
    for (const auto& size_str : sizes) {
        if (!g_running) break;
        size_t x_pos = size_str.find('x');
        if (x_pos == std::string::npos) {
            std::cout << "无效的尺寸格式: " << size_str << "，请使用 WxH 格式（如 256x256）" << std::endl;
            continue;
        }
        try {
            int width = std::stoi(size_str.substr(0, x_pos));
            int height = std::stoi(size_str.substr(x_pos + 1));
            std::string output_file = prefix + "_" + size_str + ".png";

            std::string vf;
            if (scale_mode == "1") {
                vf = "scale=" + size_str + ":force_original_aspect_ratio=decrease,pad=" + size_str + ":(ow-iw)/2:(oh-ih)/2:color=white@0";
            } else if (scale_mode == "2") {
                vf = "scale=" + size_str + ":force_original_aspect_ratio=increase,crop=" + size_str;
            } else {
                vf = "scale=" + size_str;
            }

            std::string cmd = "ffmpeg -hide_banner -i " + quote_short_path(input_file) +
                              " -vf \"" + vf + "\" -y " + quote_short_path(output_file);
            std::cout << "正在生成 " << size_str << " 尺寸..." << std::endl;
            if (run_ffmpeg(cmd)) {
                ++success_count;
                std::cout << "成功生成: " << output_file << std::endl;
            } else {
                std::cout << "生成 " << size_str << " 尺寸失败" << std::endl;
            }
        } catch (...) {
            std::cout << "处理尺寸 " << size_str << " 时发生错误" << std::endl;
        }
    }

    std::cout << "\n完成! 成功生成了 " << success_count << "/" << sizes.size() << " 个尺寸的图像" << std::endl;
    std::cout << "提示: 您可以使用 ImageMagick 的 'convert' 命令将这些PNG合并为ICO文件:" << std::endl;
    std::cout << "      convert " << prefix << "_*.png output.ico" << std::endl;
    std::cout << "\n按回车键继续...";
    std::cin.get();
}

void resize_photo() {
    std::string input_file = get_input("请输入图片文件路径: ", "", true);
    if (!g_running) return;

    std::cout << "\n请输入目标分辨率（格式: 宽度x高度，默认: 480x640）" << std::endl;
    std::string resolution = get_input("目标分辨率: ", "480x640");
    std::string output = get_input("输出文件名 [默认 resized]: ", "resized", false, true, ".jpg");
    std::string quality = get_input("图片质量 (1-100，数值越高质量越好文件越大) [默认 5]: ", "5");
    if (!g_running) return;

    std::cout << "\n缩放模式选项:" << std::endl;
    std::cout << "1. 保持宽高比（可能添加黑边）" << std::endl;
    std::cout << "2. 保持宽高比并裁剪" << std::endl;
    std::cout << "3. 拉伸图像以适应目标尺寸" << std::endl;
    std::string scale_mode = get_input("请选择缩放模式 [默认 3]: ", "3");
    if (!g_running) return;

    std::string vf;
    if (scale_mode == "1") {
        vf = "scale=" + resolution + ":force_original_aspect_ratio=decrease,pad=" + resolution + ":(ow-iw)/2:(oh-ih)/2:color=black";
    } else if (scale_mode == "2") {
        vf = "scale=" + resolution + ":force_original_aspect_ratio=increase,crop=" + resolution;
    } else {
        vf = "scale=" + resolution;
    }

    std::string cmd = "ffmpeg -hide_banner -i " + quote_short_path(input_file) +
                      " -vf \"" + vf + "\" -q:v " + quality + " -y " + quote_short_path(output);

    std::cout << "正在调整图片尺寸和质量..." << std::endl;

    size_t original_size = fs::exists(input_file) ? fs::file_size(input_file) : 0;

    if (run_ffmpeg(cmd)) {
        if (fs::exists(output)) {
            size_t new_size = fs::file_size(output);
            long long diff = static_cast<long long>(new_size) - static_cast<long long>(original_size);
            double percent = (original_size > 0) ? (static_cast<double>(new_size) / original_size * 100) : 0.0;

            std::cout << "图片处理成功: " << output << std::endl;
            std::cout << "原始大小: " << format_file_size(original_size) << std::endl;
            std::cout << "新文件大小: " << format_file_size(new_size) << std::endl;
            std::cout << "大小变化: " << format_file_size(std::abs(diff))
                      << " (" << (diff > 0 ? "增加" : "减少") << ", "
                      << std::fixed << std::setprecision(1) << percent << "%)" << std::endl;
        } else {
            std::cout << "图片处理失败" << std::endl;
        }
    } else {
        std::cout << "图片处理失败" << std::endl;
    }
    std::cout << "\n按回车键继续...";
    std::cin.get();
}

// ------------------ 主程序 ------------------
int main() {
    // 不设置任何编码，使用系统默认（GBK）
    signal(SIGINT, signal_handler);

    while (g_running) {
        show_menu();
        std::string choice;
        std::cout << "请输入选项 (1-10): ";
        std::getline(std::cin, choice);
        if (!g_running) break;

        if (choice == "1") {
            show_info();
        } else if (choice == "2") {
            extract_cover();
        } else if (choice == "3") {
            copy_audio();
        } else if (choice == "4") {
            copy_video();
        } else if (choice == "5") {
            remove_metadata();
        } else if (choice == "6") {
            add_cover();
        } else if (choice == "7") {
            merge_av();
        } else if (choice == "8") {
            generate_icon_sizes();
        } else if (choice == "9") {
            resize_photo();
        } else if (choice == "10") {
            std::cout << "感谢使用，再见！" << std::endl;
            break;
        } else {
            std::cout << "无效选项，请重新输入" << std::endl;
            std::cout << "按回车键继续...";
            std::cin.get();
        }
    }
    std::cout << "程序已退出。" << std::endl;
    return 0;
}