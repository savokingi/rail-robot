import gzip
import sys
import argparse
import re
from typing import Union


def minify_html(html_bytes: Union[bytes, bytearray], encoding: str = 'utf-8') -> bytes:
    """
    Минификация HTML, работающая с bytes-like объектами.
    
    Аргументы:
        html_bytes: входные данные (bytes, bytearray) – HTML-код.
        encoding: кодировка исходного HTML (по умолчанию utf-8).
    
    Возвращает:
        bytes – минифицированный HTML в той же кодировке.
    """
    # Декодируем байты в строку
    html_str = html_bytes.decode(encoding)
    
    # --- та же логика минификации, что и раньше ---
    # Удаляем комментарии
    html_str = re.sub(r'<!--.*?-->', '', html_str, flags=re.DOTALL)
    # Убираем пробелы между тегами
    html_str = re.sub(r'>\s+<', '><', html_str)
    # Сжимаем множественные пробелы внутри тегов
    html_str = re.sub(r'\s{2,}', ' ', html_str)
    # Убираем пробелы вокруг "=" в атрибутах
    html_str = re.sub(r'\s*=\s*', '=', html_str)
    # Убираем пробел перед закрывающим слешем в одиночных тегах
    html_str = re.sub(r'\s+/>', '/>', html_str)
    # --------------------------------------------
    
    # Кодируем обратно в байты
    return html_str.strip().encode(encoding)

def compress_html_to_gzip(data: bytes) -> bytes:
    """Compress bytes using gzip with maximum compression level."""
    return gzip.compress(data, compresslevel=9)

def format_as_c_array(data: bytes, var_name: str = "html_gz", line_width: int = 16) -> str:
    """
    Format bytes as a C-style array of hexadecimal bytes.
    Example output:
        unsigned char html_gz[] = {
            0x1F, 0x8B, 0x08, 0x00, ...
        };
    """
    hex_bytes = [f"0x{b:02X}" for b in data]
    lines = []
    for i in range(0, len(hex_bytes), line_width):
        chunk = hex_bytes[i:i+line_width]
        lines.append("    " + ", ".join(chunk))
    array_body = ",\n".join(lines)
    return f"unsigned char {var_name}[] PROGMEM = {{\n{array_body}\n}};"

def main():
    parser = argparse.ArgumentParser(
        description="Compress HTML file with gzip and output as C byte array."
    )
    parser.add_argument(
        "input_file", nargs="?", help="Path to HTML file. If omitted, read from stdin."
    )
    parser.add_argument(
        "-o", "--output", help="Output file for the C array (default: stdout)."
    )
    parser.add_argument(
        "-n", "--name", default="html_gz", help="Name of the C array variable (default: html_gz)."
    )
    parser.add_argument(
        "-w", "--width", type=int, default=16, help="Number of bytes per line (default: 16)."
    )
    args = parser.parse_args()

    # Read HTML data
    if args.input_file:
        with open(args.input_file, "rb") as f:
            html_data = f.read()
    else:
        with open("index.html", "rb") as f:
            html_data = f.read()
        #html_data = sys.stdin.buffer.read()

    if not html_data:
        print("Error: No HTML data provided.", file=sys.stderr)
        sys.exit(1)
    
    print("Input len",len(html_data))

    html_data=html_data.replace(b'\n',b'')
    html_data=html_data.replace(b'\r',b'')
    html_data=html_data.replace(b'\t',b'')
    print("Len after replace step1",len(html_data))
    html_data =minify_html(html_data)
    print("Len after replace stap2",len(html_data))
    # Compress with gzip
    compressed = compress_html_to_gzip(html_data)

    print("Compress len",len(compressed))
    # Format as C array
    c_array = format_as_c_array(compressed, var_name=args.name, line_width=args.width)
    c_array+="\nconst uint32_t html_gz_len = "+str(len(compressed))+";"
    # Output
    if args.output:
        with open(args.output, "w") as f:
            f.write(c_array)
    else:
        print(c_array)

if __name__ == "__main__":
    main()