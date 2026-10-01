class Vibematrix < Formula
  desc "Terminal visualizer: LED cube GLSL shaders driven by file changes"
  homepage "https://github.com/lad1337/vibematrix"
  url "https://github.com/lad1337/vibematrix/archive/refs/tags/v0.1.0.tar.gz"
  sha256 "74782667b8726512429c3255840c99123ce3586b230d3d7c2fd7b6583f0add05"
  head "https://github.com/lad1337/vibematrix.git", branch: "main"

  depends_on :macos # CGL offscreen OpenGL + FSEvents

  def install
    system "make", "install", "PREFIX=#{prefix}"
  end

  test do
    # parses args and finds the installed shaders
    assert_match "smoke2", shell_output("#{bin}/vibematrix --shader nope 2>&1", 1)
    # line diffing, smoothing, and every shader compiling on this machine's GL
    assert_match(/^ok$/, shell_output("#{bin}/vibematrix --test"))
  end
end
