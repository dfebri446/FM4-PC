# Windows OCR server for FM4 frame captures. Reads one request per stdin line,
# "<path.ppm> [scale]", and writes one JSON line of recognized text lines with
# boxes in source-image pixels. Kept alive to avoid WinRT startup per frame.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Runtime.WindowsRuntime
[Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType = WindowsRuntime] | Out-Null
[Windows.Graphics.Imaging.SoftwareBitmap, Windows.Graphics, ContentType = WindowsRuntime] | Out-Null
[Windows.Globalization.Language, Windows.Globalization, ContentType = WindowsRuntime] | Out-Null
Add-Type -TypeDefinition @'
using System;
using System.IO;
public static class Fm4Ppm {
  static byte[] gray_ = new byte[0];
  static byte[] red_ = new byte[0];
  static int grayWidth_, grayHeight_;
  // Per-mille of strongly red pixels (FM4's selection color) in a rectangle.
  public static int Red(int x, int y, int w, int h) {
    long sum = 0, count = 0;
    for (int yy = Math.Max(0, y); yy < Math.Min(grayHeight_, y + h); yy++)
      for (int xx = Math.Max(0, x); xx < Math.Min(grayWidth_, x + w); xx++) { sum += red_[yy * grayWidth_ + xx]; count++; }
    return count == 0 ? 0 : (int)(sum * 1000 / count);
  }
  // Mean luminance of a source-pixel rectangle of the last loaded image.
  public static int Mean(int x, int y, int w, int h) {
    long sum = 0, count = 0;
    for (int yy = Math.Max(0, y); yy < Math.Min(grayHeight_, y + h); yy++)
      for (int xx = Math.Max(0, x); xx < Math.Min(grayWidth_, x + w); xx++) { sum += gray_[yy * grayWidth_ + xx]; count++; }
    return count == 0 ? 0 : (int)(sum / count);
  }
  // Grayscale, upscaled copy of a binary PPM (P6) as BGRA8 bytes. Small white
  // UI text on dark backgrounds recognizes far better at 2x. Captures of a
  // resolution scaled run (2560x1440, 3840x2160) are first averaged down to
  // 1280x720, so boxes and the rectangles below stay in 720p pixels.
  public static byte[] Load(string path, int scale, out int width, out int height) {
    byte[] data = File.ReadAllBytes(path);
    int pos = 0;
    Func<string> token = () => {
      while (char.IsWhiteSpace((char)data[pos])) pos++;
      int start = pos;
      while (!char.IsWhiteSpace((char)data[pos])) pos++;
      return System.Text.Encoding.ASCII.GetString(data, start, pos - start);
    };
    if (token() != "P6") throw new InvalidDataException("not P6");
    int sw = int.Parse(token()), sh = int.Parse(token());
    token();
    pos++;
    int k = Math.Max(1, sh / 720);
    int w = sw / k, h = sh / k;
    width = w * scale;
    height = h * scale;
    byte[] output = new byte[width * height * 4];
    gray_ = new byte[w * h];
    red_ = new byte[w * h];
    grayWidth_ = w;
    grayHeight_ = h;
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        int r = 0, gr = 0, b = 0;
        for (int ky = 0; ky < k; ky++)
          for (int kx = 0; kx < k; kx++) {
            int s = pos + ((y * k + ky) * sw + x * k + kx) * 3;
            r += data[s]; gr += data[s + 1]; b += data[s + 2];
          }
        r /= k * k; gr /= k * k; b /= k * k;
        byte g = (byte)((r * 77 + gr * 150 + b * 29) >> 8);
        gray_[y * w + x] = g;
        red_[y * w + x] = (byte)(r > 150 && gr < 80 && b < 80 ? 1 : 0);
        for (int dy = 0; dy < scale; dy++) {
          int d = ((y * scale + dy) * width + x * scale) * 4;
          for (int dx = 0; dx < scale; dx++, d += 4) {
            output[d] = g; output[d + 1] = g; output[d + 2] = g; output[d + 3] = 255;
          }
        }
      }
    }
    return output;
  }
}
'@
$asTask = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
  $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
  $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' } | Select-Object -First 1
function Await($operation, [Type]$type) {
  $task = $asTask.MakeGenericMethod($type).Invoke($null, @($operation))
  $task.Wait(-1) | Out-Null
  $task.Result
}
$engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage([Windows.Globalization.Language]::new('en-US'))
[Console]::Out.WriteLine('{"ready":true}')
[Console]::Out.Flush()
while ($null -ne ($request = [Console]::In.ReadLine())) {
  try {
    $parts = $request -split '\|'
    $scale = if ($parts.Count -gt 1) { [int]$parts[1] } else { 2 }
    $width = 0; $height = 0
    $pixels = [Fm4Ppm]::Load($parts[0], $scale, [ref]$width, [ref]$height)
    $buffer = [System.Runtime.InteropServices.WindowsRuntime.WindowsRuntimeBufferExtensions]::AsBuffer($pixels)
    $bitmap = [Windows.Graphics.Imaging.SoftwareBitmap]::CreateCopyFromBuffer($buffer,
      [Windows.Graphics.Imaging.BitmapPixelFormat]::Bgra8, $width, $height)
    $result = Await ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])
    $lines = foreach ($line in $result.Lines) {
      $boxes = $line.Words | ForEach-Object { $_.BoundingRect }
      $x0 = ($boxes | Measure-Object -Property X -Minimum).Minimum
      $y0 = ($boxes | Measure-Object -Property Y -Minimum).Minimum
      $x1 = ($boxes | ForEach-Object { $_.X + $_.Width } | Measure-Object -Maximum).Maximum
      $y1 = ($boxes | ForEach-Object { $_.Y + $_.Height } | Measure-Object -Maximum).Maximum
      $bx = [int]($x0 / $scale); $by = [int]($y0 / $scale)
      $bw = [int](($x1 - $x0) / $scale); $bh = [int](($y1 - $y0) / $scale)
      [ordered]@{ text = $line.Text; x = $bx; y = $by; w = $bw; h = $bh
                  bg = [Fm4Ppm]::Mean($bx - 6, $by - 4, $bw + 12, $bh + 8)
                  red = [Fm4Ppm]::Red($bx - 70, $by - 14, $bw + 140, $bh + 34)
                  red_text = [Fm4Ppm]::Red($bx, $by, $bw, $bh)
                  # A highlighted entry of a vertical menu has a red line above and below it.
                  red_above = [Fm4Ppm]::Red($bx - 70, $by - 16, $bw + 140, 12)
                  red_below = [Fm4Ppm]::Red($bx - 70, $by + $bh + 2, $bw + 140, 14) }
    }
    $bitmap.Dispose()
    [Console]::Out.WriteLine((ConvertTo-Json -InputObject @{ lines = @($lines) } -Compress -Depth 4))
  } catch {
    [Console]::Out.WriteLine((ConvertTo-Json -InputObject @{ error = $_.Exception.Message } -Compress))
  }
  [Console]::Out.Flush()
}
