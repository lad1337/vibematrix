class Vibematrix < Formula
  desc "Terminal visualizer: LED cube GLSL shaders driven by file changes"
  homepage "https://github.com/lad1337/vibematrix"
  url "https://github.com/lad1337/vibematrix/archive/refs/tags/v0.2.1.tar.gz"
  sha256 "0f969b98282e85b272d525addc5571102b71e53995563032edda0402c76d7d15"
  head "https://github.com/lad1337/vibematrix.git", branch: "main"

  depends_on :macos # CGL offscreen OpenGL + FSEvents

  def install
    system "make", "install", "PREFIX=#{prefix}", "VERSION=#{version}"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/vibematrix --version")
    # parses args and finds the installed shaders
    assert_match "smoke2", shell_output("#{bin}/vibematrix --shader nope 2>&1", 1)
    # line diffing, smoothing, and every shader compiling on this machine's GL
    assert_match(/^ok$/, shell_output("#{bin}/vibematrix --test"))
  end
end
