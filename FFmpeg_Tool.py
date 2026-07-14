import re
import time
import os
import subprocess
import sys
from pathlib import Path
from datetime import datetime, timedelta

def clear_screen():
    os.system('cls' if os.name == 'nt' else 'clear')

def show_menu():
    clear_screen()
    print("""
    FFmpeg工具箱 - 请选择操作：
    1. 显示媒体详细信息
    2. 提取封面图片
    3. 复制音频流
    4. 复制视频流
    5. 删除元数据章节
    6. 添加封面图片
    7. 合并视频和音频流
    8. 生成多尺寸图标（用于ICO）
    9. 调整图片尺寸和质量
    10. 退出
    """)

def get_input(prompt, default=None, is_path=False, is_output=False, default_ext=None):
    while True:
        user_input = input(prompt).strip()
        if user_input:
            if is_path and not Path(user_input).exists():
                print(f"文件不存在: {user_input}")
                continue
            if is_output and default_ext and '.' not in user_input:
                user_input += default_ext
            return user_input
        if default is not None:
            if is_output and default_ext and '.' not in default:
                default += default_ext
            return default
        print("输入不能为空，请重新输入")

def run_ffmpeg(command):
    try:
        result = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding='utf-8',
            errors='replace'
        )
        if result.returncode != 0:
            print("执行出错:")
            print(result.stdout)
        return result.returncode == 0
    except Exception as e:
        print(f"发生异常: {str(e)}")
        return False

def show_info():
    input_file = get_input("请输入媒体文件路径: ", is_path=True)
    pwsh = f'ffmpeg -i "{input_file}"'
    print("正在获取媒体信息...")
    os.system(pwsh)
    input("\n按回车键继续...")

def extract_cover():
    input_file = get_input("请输入视频文件路径: ", is_path=True)
    stream = get_input("输入封面流索引 [默认 0:2]: ", default="0:2")
    output = get_input("输出图片名称 [默认 cover]: ", default="cover", is_output=True, default_ext=".png")
    
    total_duration = get_video_duration(input_file)
    
    pwsh = [
        'ffmpeg',
        '-hide_banner',
        '-i', input_file,
        '-map', stream,
        '-c', 'copy',
        '-progress', 'pipe:1',
        '-y',
        output
    ]
    
    print("正在提取封面...")
    success = run_ffmpeg_with_progress(pwsh, total_duration)
    
    if Path(output).exists():
        print(f"封面提取成功: {output}")
    else:
        print("封面提取失败，请检查流索引")
    input("\n按回车键继续...")

def copy_audio():
    input_file = get_input("请输入视频文件路径: ", is_path=True)
    audio_stream = get_input("输入音频流索引 [默认 0:1]: ", default="0:1")
    output = get_input("输出音频文件名称 [默认 audio]: ", default="audio", is_output=True, default_ext=".aac")
    
    total_duration = get_video_duration(input_file)
    
    pwsh = [
        'ffmpeg',
        '-hide_banner',
        '-i', input_file,
        '-map', audio_stream,
        '-c', 'copy',
        '-progress', 'pipe:1',
        '-y',
        output
    ]
    
    print("正在提取音频...")
    success = run_ffmpeg_with_progress(pwsh, total_duration)
    
    if Path(output).exists():
        print(f"音频提取成功: {output}")
    else:
        print("音频提取失败，请检查流索引")
    input("\n按回车键继续...")

def copy_video():
    input_file = get_input("请输入视频文件路径: ", is_path=True)
    video_stream = get_input("输入视频流索引 [默认 0:0]: ", default="0:0")
    output = get_input("输出视频文件名称 [默认 video]: ", default="video", is_output=True, default_ext=".mp4")
    
    total_duration = get_video_duration(input_file)
    
    pwsh = [
        'ffmpeg',
        '-hide_banner',
        '-i', input_file,
        '-map', video_stream,
        '-c', 'copy',
        '-progress', 'pipe:1',
        '-y',
        output
    ]
    
    print("正在提取视频流...")
    success = run_ffmpeg_with_progress(pwsh, total_duration)
    
    if Path(output).exists():
        print(f"视频流提取成功: {output}")
    else:
        print("视频流提取失败，请检查流索引")
    input("\n按回车键继续...")

def remove_metadata():
    input_file = get_input("请输入源文件路径: ", is_path=True)
    video_streams_input = get_input("输入要保留的视频流（逗号分隔，默认 0:0）: ", default="0:0")
    audio_streams_input = get_input("输入要保留的音频流（逗号分隔，默认 0:1）: ", default="0:1")
    output = get_input("输出文件名 [默认 output]: ", default="output", is_output=True, default_ext=".mp4")
    
    video_streams = [s.strip() for s in video_streams_input.split(',')]
    audio_streams = [s.strip() for s in audio_streams_input.split(',')]
    
    total_duration = get_video_duration(input_file)
    
    pwsh = [
        'ffmpeg',
        '-hide_banner',
        '-i', input_file,
        '-map_metadata', '-1',
        '-map_chapters', '-1',
    ]
    
    for vs in video_streams:
        pwsh += ['-map', vs]
    for as_ in audio_streams:
        pwsh += ['-map', as_]
    
    pwsh += [
        '-codec', 'copy',
        '-progress', 'pipe:1',
        '-y',
        output
    ]
    
    print("正在处理元数据...")
    success = run_ffmpeg_with_progress(pwsh, total_duration)
    
    if success:
        print(f"处理完成: {output}")
    input("\n按回车键继续...")

def add_cover():
    video = get_input("请输入视频文件路径: ", is_path=True)
    cover = get_input("请输入封面图片路径: ", is_path=True)
    output = get_input("输出文件名 (默认 output): ", default="output", is_output=True, default_ext=".mp4")
    
    total_duration = get_video_duration(video)
    
    pwsh = [
        'ffmpeg',
        '-hide_banner',
        '-i', video,
        '-i', cover,
        '-map', '0',
        '-map', '1',
        '-c', 'copy',
        '-disposition:v:1', 'attached_pic',
        '-progress', 'pipe:1',
        '-y',
        output
    ]
    
    print("正在添加封面...")
    success = run_ffmpeg_with_progress(pwsh, total_duration)
    
    if success:
        print(f"封面添加成功: {output}")
    input("\n按回车键继续...")

def merge_av():
    video_file = get_input("请输入视频文件路径: ", is_path=True)
    audio_file = get_input("请输入音频文件路径: ", is_path=True)
    output = get_input("输出文件名 (默认 merged): ", default="merged", is_output=True, default_ext=".mp4")
    
    # 使用视频时长作为进度参考
    total_duration = get_video_duration(video_file)
    
    pwsh = [
        'ffmpeg',
        '-hide_banner',
        '-i', video_file,
        '-i', audio_file,
        '-map', '0:v:0',  # 第一个输入文件的视频流
        '-map', '1:a:0',  # 第二个输入文件的音频流
        '-c', 'copy',
        '-progress', 'pipe:1',
        '-y',
        output
    ]
    
    print("正在合并视频和音频...")
    success = run_ffmpeg_with_progress(pwsh, total_duration)
    
    if success:
        print(f"合并完成: {output}")
    else:
        print("合并失败，请检查输入文件")
    input("\n按回车键继续...")

def parse_duration(time_str):
    pattern = r"(\d+):(\d+):(\d+\.\d+)"
    match = re.match(pattern, time_str)
    if not match:
        return 0
    hours, minutes, seconds = map(float, match.groups())
    return hours * 3600 + minutes * 60 + seconds

def get_video_duration(input_file):
    pwsh = [
        'ffprobe',
        '-v', 'error',
        '-show_entries', 'format=duration',
        '-of', 'default=noprint_wrappers=1:nokey=1',
        input_file
    ]
    try:
        result = subprocess.run(pwsh, capture_output=True, text=True, check=True)
        return float(result.stdout.strip())
    except (subprocess.CalledProcessError, ValueError):
        return 0

def run_ffmpeg_with_progress(pwsh, total_duration):
    start_time = time.time()
    process = subprocess.Popen(
        pwsh,
        stderr=subprocess.PIPE,
        universal_newlines=True,
        encoding='utf-8',
        errors='replace'
    )

    progress_pattern = re.compile(r"time=(\d+:\d+:\d+\.\d+)")
    speed_pattern = re.compile(r"speed=([\d.]+)x")

    print("\n转换进度：")
    print("[0%] 正在初始化...", end='\r')

    while True:
        line = process.stderr.readline()
        if not line:
            break

        time_match = progress_pattern.search(line)
        if time_match and total_duration > 0:
            current_time = parse_duration(time_match.group(1))
            progress = min(current_time / total_duration, 1.0)
            
            elapsed = time.time() - start_time
            speed_match = speed_pattern.search(line)
            speed = float(speed_match.group(1)) if speed_match else 1.0
            remaining = (total_duration - current_time) / speed if speed > 0 else 0
            
            progress_bar = '▓' * int(progress * 20) + '░' * (20 - int(progress * 20))
            time_info = (
                f"已用: {timedelta(seconds=int(elapsed))} | "
                f"剩余: {timedelta(seconds=int(remaining))} | "
                f"速度: {speed:.1f}x"
            )
            print(
                f"[{progress:.1%}] {progress_bar} | {time_info}",
                end='\r'
            )

    process.wait()
    print("\n\n转换完成！" if process.returncode == 0 else "\n\n转换失败！")
    return process.returncode == 0

def generate_icon_sizes():
    input_file = get_input("请输入源图片路径: ", is_path=True)
    
    # 获取常用ICO尺寸或自定义尺寸
    print("\n常用ICO尺寸: 16x16, 32x32, 48x48, 64x64, 128x128, 256x256")
    sizes_input = get_input("请输入尺寸列表（用逗号分隔，例如: 256x256,128x128,64x64）: ")
    sizes = [size.strip() for size in sizes_input.split(',')]
    
    # 获取输出文件名前缀
    prefix = get_input("请输入输出文件前缀 [默认 'icon']: ", default="icon")
    
    # 获取缩放模式
    print("\n缩放模式选项:")
    print("1. 保持宽高比并填充为正方形（透明背景）")
    print("2. 保持宽高比并裁剪为正方形")
    print("3. 拉伸图像以适应目标尺寸（可能变形）")
    scale_mode = get_input("请选择缩放模式 [默认 1]: ", default="1")
    
    # 处理每个尺寸
    success_count = 0
    for size in sizes:
        try:
            width, height = size.split('x')
            width = int(width)
            height = int(height)
            
            output_file = f"{prefix}_{width}x{height}.png"
            
            # 根据选择的缩放模式构建不同的滤镜
            if scale_mode == "1":
                # 保持宽高比并填充为正方形
                vf = f"scale={width}:{height}:force_original_aspect_ratio=decrease,pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:color=white@0"
            elif scale_mode == "2":
                # 保持宽高比并裁剪为正方形
                vf = f"scale={width}:{height}:force_original_aspect_ratio=increase,crop={width}:{height}"
            else:
                # 拉伸图像以适应目标尺寸
                vf = f"scale={width}:{height}"
            
            pwsh = [
                'ffmpeg',
                '-hide_banner',
                '-i', input_file,
                '-vf', vf,
                '-y',
                output_file
            ]
            
            print(f"正在生成 {size} 尺寸...")
            if run_ffmpeg(pwsh):
                success_count += 1
                print(f"成功生成: {output_file}")
            else:
                print(f"生成 {size} 尺寸失败")
                
        except ValueError:
            print(f"无效的尺寸格式: {size}，请使用 WxH 格式（如 256x256）")
    
    print(f"\n完成! 成功生成了 {success_count}/{len(sizes)} 个尺寸的图像")
    print("提示: 您可以使用 ImageMagick 的 'convert' 命令将这些PNG合并为ICO文件:")
    print(f"      convert {prefix}_*.png output.ico")
    input("\n按回车键继续...")

def resize_photo():
    input_file = get_input("请输入图片文件路径: ", is_path=True)
    
    # 获取目标分辨率
    print("\n请输入目标分辨率（格式: 宽度x高度，默认: 480x640）")
    resolution = get_input("目标分辨率: ", default="480x640")
    
    # 获取输出文件名
    output = get_input("输出文件名 [默认 resized]: ", default="resized", is_output=True, default_ext=".jpg")
    
    # 获取图片质量（用于控制文件大小）
    quality = get_input("图片质量 (1-100，数值越高质量越好文件越大) [默认 5]: ", default="5")
    
    # 获取缩放模式
    print("\n缩放模式选项:")
    print("1. 保持宽高比（可能添加黑边）")
    print("2. 保持宽高比并裁剪")
    print("3. 拉伸图像以适应目标尺寸")
    scale_mode = get_input("请选择缩放模式 [默认 3]: ", default="3")
    
    # 构建FFmpeg命令
    if scale_mode == "1":
        # 保持宽高比并填充
        vf = f"scale={resolution}:force_original_aspect_ratio=decrease,pad={resolution}:(ow-iw)/2:(oh-ih)/2:color=black"
    elif scale_mode == "2":
        # 保持宽高比并裁剪
        vf = f"scale={resolution}:force_original_aspect_ratio=increase,crop={resolution}"
    else:
        # 拉伸图像以适应目标尺寸
        vf = f"scale={resolution}"
    
    pwsh = [
        'ffmpeg',
        '-hide_banner',
        '-i', input_file,
        '-vf', vf,
        '-q:v', quality,  # 设置图片质量
        '-y',
        output
    ]
    
    print("正在调整图片尺寸和质量...")
    
    # 获取原始文件大小
    original_size = os.path.getsize(input_file) if os.path.exists(input_file) else 0
    
    if run_ffmpeg(pwsh):
        if Path(output).exists():
            # 获取新文件大小
            new_size = os.path.getsize(output)
            size_change = new_size - original_size
            size_percent = (new_size / original_size * 100) if original_size > 0 else 0
            
            print(f"图片处理成功: {output}")
            print(f"原始大小: {format_file_size(original_size)}")
            print(f"新文件大小: {format_file_size(new_size)}")
            print(f"大小变化: {format_file_size(abs(size_change))} ({'增加' if size_change > 0 else '减少'}, {size_percent:.1f}%)")
        else:
            print("图片处理失败")
    else:
        print("图片处理失败")
    
    input("\n按回车键继续...")

def format_file_size(size_bytes):
    """将字节数转换为更易读的格式"""
    if size_bytes == 0:
        return "0B"
    
    size_names = ["B", "KB", "MB", "GB"]
    i = 0
    size = float(size_bytes)
    
    while size >= 1024 and i < len(size_names) - 1:
        size /= 1024
        i += 1
        
    return f"{size:.2f} {size_names[i]}"

def main():
    while True:
        show_menu()
        choice = input("请输入选项 (1-10): ").strip()
        
        if choice == '1':
            show_info()
        elif choice == '2':
            extract_cover()
        elif choice == '3':
            copy_audio()
        elif choice == '4':
            copy_video()
        elif choice == '5':
            remove_metadata()
        elif choice == '6':
            add_cover()
        elif choice == '7':
            merge_av()
        elif choice == '8':  # 生成多尺寸图标
            generate_icon_sizes()
        elif choice == '9':  # 新增的调整图片尺寸功能
            resize_photo()
        elif choice == '10':
            print("感谢使用，再见！")
            sys.exit()
        else:
            print("无效选项，请重新输入")
            input("按回车键继续...")

    
if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n操作已取消")
        sys.exit()
