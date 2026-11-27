// SPDX-License-Identifier: ISC
/*
 * ares_harness.cpp - Unified headless test harness for Sega Mega Drive /
 * Genesis, Sega CD / Mega CD, Sega 32X, and Mega CD 32X using ares.
 *
 * Defaults to pure SH-2 interpreter mode ("Recompiler" = false) for accurate
 * SH7604 cache/CCR, RV bus arbitration, CRAM PEN/VBlank, and SSF verification.
 *
 * Modes:
 *   Script mode:     ares-harness [opts] <rom> <script.txt> <outdir>
 *   Inline mode:     ares-harness --inline [opts] <rom> <frames> <directives> <prefix>
 *   Comm-check mode: ares-harness --comm-check [opts] <rom.32x> [frames]
 */

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#define MIA_LIBRARY
#include <mia/mia.hpp>

// `ares/ares.hpp` aliases `register` to `$register` for emulated register structs;
// undefine it before regular C++ harness code so nothing collides unexpectedly.
#undef register

#include "ares_harness_mem.hpp"

namespace fs = std::filesystem;

namespace {

enum PadButton : unsigned {
  BTN_UP    = 0,
  BTN_DOWN  = 1,
  BTN_LEFT  = 2,
  BTN_RIGHT = 3,
  BTN_A     = 4,
  BTN_B     = 5,
  BTN_C     = 6,
  BTN_START = 7,
  BTN_X     = 8,
  BTN_Y     = 9,
  BTN_Z     = 10,
  BTN_MODE  = 11,
};

enum class SystemKind {
  Auto,
  MegaDrive,
  Mega32X,
  MegaCD,
  MegaCD32X,
};

enum class ShotFormat {
  BMP,
  PPM,
};

struct Options {
  SystemKind system = SystemKind::Auto;
  std::string region;
  std::string pad_type = "fighting";
  std::string bios_path;
  std::string sram_dir;
  std::string dump_sram;
  std::string wav_path;
  bool use_recompiler = false;
  bool use_tmss = false;
  bool inline_mode = false;
  bool comm_check = false;
  bool format_explicit = false;
  ShotFormat format = ShotFormat::BMP;
};

struct ButtonMapping {
  const char *name;
  unsigned bit;
};

static constexpr ButtonMapping kBtnTable[] = {
  {"up",    BTN_UP},
  {"down",  BTN_DOWN},
  {"left",  BTN_LEFT},
  {"right", BTN_RIGHT},
  {"a",     BTN_A},
  {"b",     BTN_B},
  {"c",     BTN_C},
  {"start", BTN_START},
  {"x",     BTN_X},
  {"y",     BTN_Y},
  {"z",     BTN_Z},
  {"mode",  BTN_MODE},
};

/** Map a script button name (e.g. "start", "a", "z") to its PadButton bit index. */
static int lookup_button_bit(const std::string &name) {
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  for (const auto &entry : kBtnTable) {
    if (lower == entry.name) return static_cast<int>(entry.bit);
  }
  return -1;
}

/** Lowercase ASCII helper for extensions and region checks. */
static std::string to_lower_ascii(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

/** Trim leading and trailing ASCII whitespace. */
static std::string trim_ws(const std::string &s) {
  size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

/** Split string by single delimiter character. */
static std::vector<std::string> split_char(const std::string &s, char delim) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= s.size()) {
    size_t pos = s.find(delim, start);
    if (pos == std::string::npos) {
      out.push_back(s.substr(start));
      break;
    }
    out.push_back(s.substr(start, pos - start));
    start = pos + 1;
  }
  return out;
}

/** Write little-endian 32-bit integer to byte buffer at offset. */
static void put_le32(uint8_t *dst, uint32_t v) {
  dst[0] = static_cast<uint8_t>(v & 0xFFu);
  dst[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
  dst[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
  dst[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}

/** Write little-endian 16-bit integer to byte buffer at offset. */
static void put_le16(uint8_t *dst, uint16_t v) {
  dst[0] = static_cast<uint8_t>(v & 0xFFu);
  dst[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
}

/** Headless implementation of ares::Platform capturing video, audio, and 6-button inputs. */
struct HeadlessPlatform final : ares::Platform {
  std::shared_ptr<mia::Pak> system_pak;
  std::shared_ptr<mia::Pak> game_pak;
  std::shared_ptr<mia::Pak> disc_pak;
  std::vector<ares::Node::Audio::Stream> streams;

  uint32_t pad[2] = {0, 0};

  std::mutex video_mutex;
  std::condition_variable video_cv;
  std::vector<uint8_t> rgb;
  unsigned frame_w = 0;
  unsigned frame_h = 0;
  uint64_t video_frames = 0;

  uint64_t audio_samples_total = 0;
  uint64_t audio_samples_nonzero = 0;
  int audio_peak = 0;
  uint64_t audio_energy = 0;
  uint64_t interval_samples = 0;
  uint64_t interval_energy = 0;

  FILE *wav_file = nullptr;
  uint32_t wav_data_bytes = 0;

  /** Close WAV capture file and finalize RIFF chunk sizes. */
  ~HeadlessPlatform() {
    close_wav();
  }

  /** Open 16-bit stereo 48 kHz WAV capture output if path is non-empty. */
  bool open_wav(const std::string &path) {
    if (path.empty()) return true;
    wav_file = std::fopen(path.c_str(), "wb");
    if (!wav_file) {
      std::perror(path.c_str());
      return false;
    }
    // Reserve 44-byte RIFF/WAVE header; patched cleanly in close_wav().
    uint8_t hdr[44] = {
      'R','I','F','F', 0,0,0,0, 'W','A','V','E',
      'f','m','t',' ', 16,0,0,0, 1,0, 2,0,
      0x80,0xBB,0x00,0x00, 0x00,0xEE,0x02,0x00, 4,0, 16,0,
      'd','a','t','a', 0,0,0,0
    };
    std::fwrite(hdr, 1, sizeof(hdr), wav_file);
    wav_data_bytes = 0;
    return true;
  }

  /** Finalize WAV sizes and close WAV file handle. */
  void close_wav() {
    if (!wav_file) return;
    uint32_t riff_size = 36u + wav_data_bytes;
    std::fseek(wav_file, 4, SEEK_SET);
    uint8_t sz[4];
    put_le32(sz, riff_size);
    std::fwrite(sz, 1, 4, wav_file);
    std::fseek(wav_file, 40, SEEK_SET);
    put_le32(sz, wav_data_bytes);
    std::fwrite(sz, 1, 4, wav_file);
    std::fclose(wav_file);
    wav_file = nullptr;
  }

  /** Configure attached screen/audio nodes (disable overscan, fix 48 kHz audio resampling). */
  auto attach(ares::Node::Object node) -> void override {
    if (auto screen = node->cast<ares::Node::Video::Screen>()) {
      // Disable overscan so Mega Drive / 32X delivers active 320x224 (or 256x224) viewport.
      screen->setOverscan(false);
    }
    if (auto stream = node->cast<ares::Node::Audio::Stream>()) {
      stream->setResamplerFrequency(48000.0);
      if (std::find(streams.begin(), streams.end(), stream) == streams.end()) {
        streams.push_back(stream);
      }
    }
  }

  /** Remove detached audio streams cleanly during unload. */
  auto detach(ares::Node::Object node) -> void override {
    if (auto stream = node->cast<ares::Node::Audio::Stream>()) {
      std::erase(streams, stream);
    }
  }

  /** Supply VFS pak directories to MegaDrive::load(). */
  auto pak(ares::Node::Object node) -> std::shared_ptr<vfs::directory> override {
    if (node->name() == "Mega Drive" && system_pak) return system_pak->pak;
    if (node->name() == "Mega Drive Cartridge" && game_pak) return game_pak->pak;
    if (node->name() == "Mega CD Disc" && disc_pak) return disc_pak->pak;
    return {};
  }

  /** Consume ARGB8888 output from VDP/Screen thread and convert to tightly packed RGB888. */
  auto video(ares::Node::Video::Screen, const u32 *data, u32 pitch, u32 width, u32 height) -> void override {
    if (!data || width == 0 || height == 0) return;

    // Mega Drive VDP renders active lines inside a 1280px logical horizontal span
    // (4x pixel duplication in H40/32X = 320px, 5x in H32 = 256px) and 2x vertical
    // duplication when interlaced.
    unsigned out_w = width;
    unsigned step_x = 1;
    if (width == 1280) {
      out_w = md_vdp_is_h40() ? 320u : 256u;
      step_x = width / out_w;
    }
    unsigned step_y = (height > 256u) ? 2u : 1u;
    unsigned out_h = height / step_y;
    const unsigned stride_u32 = pitch / sizeof(u32);

    std::lock_guard<std::mutex> lock(video_mutex);
    if (out_w <= 2048 && out_h <= 1024) {
      rgb.resize(static_cast<size_t>(out_w) * out_h * 3u);
      for (unsigned y = 0; y < out_h; ++y) {
        const u32 *row = data + static_cast<size_t>(y * step_y) * stride_u32;
        uint8_t *dst = rgb.data() + static_cast<size_t>(y) * out_w * 3u;
        for (unsigned x = 0; x < out_w; ++x) {
          uint32_t v = row[x * step_x];
          // Screen::refreshPalette packs pixels as (a << 24 | r << 16 | g << 8 | b).
          dst[x * 3u + 0u] = static_cast<uint8_t>((v >> 16) & 0xFFu);
          dst[x * 3u + 1u] = static_cast<uint8_t>((v >>  8) & 0xFFu);
          dst[x * 3u + 2u] = static_cast<uint8_t>( v        & 0xFFu);
        }
      }
      frame_w = out_w;
      frame_h = out_h;
    }
    ++video_frames;
    video_cv.notify_all();
  }

  /** Drain all attached 48 kHz audio streams whenever every stream has pending samples. */
  auto audio(ares::Node::Audio::Stream) -> void override {
    if (streams.empty()) return;

    while (true) {
      for (const auto &stream : streams) {
        if (!stream->pending()) return;
      }

      double mixed[2] = {0.0, 0.0};
      for (const auto &stream : streams) {
        double buf[2] = {0.0, 0.0};
        u32 ch = stream->read(buf);
        if (ch == 1) {
          mixed[0] += buf[0];
          mixed[1] += buf[0];
        } else {
          mixed[0] += buf[0];
          mixed[1] += buf[1];
        }
      }

      int16_t pcm[2];
      for (int c = 0; c < 2; ++c) {
        double clamped = std::max(-1.0, std::min(1.0, mixed[c]));
        int val = static_cast<int>(std::llround(clamped * 32767.0));
        if (val < -32768) val = -32768;
        if (val > 32767) val = 32767;
        pcm[c] = static_cast<int16_t>(val);

        int mag = val < 0 ? -val : val;
        ++audio_samples_total;
        ++interval_samples;
        if (val != 0) ++audio_samples_nonzero;
        if (mag > audio_peak) audio_peak = mag;
        audio_energy += static_cast<uint64_t>(mag);
        interval_energy += static_cast<uint64_t>(mag);
      }

      if (wav_file) {
        uint8_t le[4];
        put_le16(&le[0], static_cast<uint16_t>(pcm[0]));
        put_le16(&le[2], static_cast<uint16_t>(pcm[1]));
        std::fwrite(le, 1, 4, wav_file);
        wav_data_bytes += 4u;
      }
    }
  }

  /** Update Controller Port 1/2 Button nodes directly from scripted pad bitmasks. */
  auto input(ares::Node::Input::Input node) -> void override {
    auto btn = node->cast<ares::Node::Input::Button>();
    if (!btn) return;

    auto device = ares::Node::parent(node);
    auto port = device ? ares::Node::parent(device) : ares::Node::Object{};
    if (!port) {
      btn->setValue(false);
      return;
    }

    int pidx = -1;
    if (port->name() == "Controller Port 1") pidx = 0;
    else if (port->name() == "Controller Port 2") pidx = 1;
    else {
      // System controls (e.g., Console Reset) stay unpressed during headless test runs.
      btn->setValue(false);
      return;
    }

    int bit = lookup_button_bit(std::string(node->name().data(), node->name().size()));
    bool pressed = (bit >= 0) && ((pad[pidx] & (1u << bit)) != 0u);
    btn->setValue(pressed);
  }
};

/** Inspect ROM header to disambiguate Sega 32X vs flat Genesis/MD or Sega CD binaries. */
static SystemKind detect_system_kind(const std::string &rom_path, const Options &opt,
                                     std::shared_ptr<mia::Pak> &out_disc) {
  out_disc.reset();
  if (opt.system != SystemKind::Auto) return opt.system;

  std::string ext = to_lower_ascii(fs::path(rom_path).extension().string());
  if (ext == ".32x") return SystemKind::Mega32X;
  if (ext == ".chd" || ext == ".cue" || ext == ".iso") return SystemKind::MegaCD;

  std::ifstream ifs(rom_path, std::ios::binary);
  if (ifs) {
    uint8_t hdr[0x400] = {};
    ifs.read(reinterpret_cast<char *>(hdr), sizeof(hdr));
    std::streamsize got = ifs.gcount();
    // Standard 32X headers advertise "SEGA 32X" at 0x100 or "MARS" signature at 0x3C0.
    if (got >= 0x108 && std::memcmp(&hdr[0x100], "SEGA 32X", 8) == 0) {
      return SystemKind::Mega32X;
    }
    if (got >= 0x3C4 && std::memcmp(&hdr[0x3C0], "MARS", 4) == 0) {
      return SystemKind::Mega32X;
    }
  }
  return SystemKind::MegaDrive;
}

/** Pick region using desktop-ui priority (NTSC-U > NTSC-J > PAL) unless overridden. */
static std::string select_region(const std::string &region_list, const std::string &override_region) {
  if (!override_region.empty()) return override_region;
  static const char *const kPref[] = {"NTSC-U", "NTSC-J", "PAL"};
  for (const char *cand : kPref) {
    if (region_list.find(cand) != std::string::npos) return cand;
  }
  return "NTSC-U";
}

/** Locate Sega CD / Mega CD BIOS image from --bios or regional working-directory names. */
static std::string resolve_cd_bios(const Options &opt, const std::string &region) {
  if (!opt.bios_path.empty() && fs::exists(opt.bios_path)) return opt.bios_path;
  std::vector<std::string> candidates;
  if (region == "PAL") {
    candidates = {"bios_CD_E.bin", "eu_mcd2_9306.bin", "bios_CD_U.bin", "bios_CD_J.bin"};
  } else if (region == "NTSC-J") {
    candidates = {"bios_CD_J.bin", "jp_mcd1_9112.bin", "bios_CD_U.bin", "bios_CD_E.bin"};
  } else {
    candidates = {"bios_CD_U.bin", "us_scd2_9306.bin", "bios_CD_E.bin", "bios_CD_J.bin"};
  }
  for (const auto &c : candidates) {
    if (fs::exists(c)) return c;
  }
  return "";
}

/** Write latest RGB framebuffer to disk as a 24-bit uncompressed BMP. */
static bool write_bmp(const HeadlessPlatform &plat, const std::string &path) {
  if (plat.frame_w == 0 || plat.frame_h == 0 || plat.rgb.empty()) {
    std::fprintf(stderr, "write_bmp: no video frame captured yet (%s)\n", path.c_str());
    return false;
  }
  FILE *f = std::fopen(path.c_str(), "wb");
  if (!f) {
    std::perror(path.c_str());
    return false;
  }

  unsigned w = plat.frame_w;
  unsigned h = plat.frame_h;
  uint32_t rowbytes = (w * 3u + 3u) & ~3u;
  uint32_t pixbytes = rowbytes * h;
  uint32_t filesize = 54u + pixbytes;

  uint8_t hdr[54] = {0};
  hdr[0] = 'B'; hdr[1] = 'M';
  put_le32(&hdr[2], filesize);
  put_le32(&hdr[10], 54u);
  put_le32(&hdr[14], 40u);
  put_le32(&hdr[18], w);
  // Negative height produces a top-down DIB matching PicoDrive harness output.
  put_le32(&hdr[22], static_cast<uint32_t>(-static_cast<int32_t>(h)));
  put_le16(&hdr[26], 1u);
  put_le16(&hdr[28], 24u);
  put_le32(&hdr[34], pixbytes);
  std::fwrite(hdr, 1, sizeof(hdr), f);

  std::vector<uint8_t> row(rowbytes, 0);
  for (unsigned y = 0; y < h; ++y) {
    const uint8_t *src = plat.rgb.data() + static_cast<size_t>(y) * w * 3u;
    for (unsigned x = 0; x < w; ++x) {
      row[x * 3u + 0u] = src[x * 3u + 2u]; // B
      row[x * 3u + 1u] = src[x * 3u + 1u]; // G
      row[x * 3u + 2u] = src[x * 3u + 0u]; // R
    }
    std::fwrite(row.data(), 1, rowbytes, f);
  }
  std::fclose(f);
  return true;
}

/** Write latest RGB framebuffer to disk as binary P6 PPM. */
static bool write_ppm(const HeadlessPlatform &plat, const std::string &path) {
  if (plat.frame_w == 0 || plat.frame_h == 0 || plat.rgb.empty()) {
    std::fprintf(stderr, "write_ppm: no video frame captured yet (%s)\n", path.c_str());
    return false;
  }
  FILE *f = std::fopen(path.c_str(), "wb");
  if (!f) {
    std::perror(path.c_str());
    return false;
  }
  std::fprintf(f, "P6\n%u %u\n255\n", plat.frame_w, plat.frame_h);
  std::fwrite(plat.rgb.data(), 1, static_cast<size_t>(plat.frame_w) * plat.frame_h * 3u, f);
  std::fclose(f);
  return true;
}

/** Advance emulated machine by one full video frame and wait for threaded Screen output. */
static void step_frame(ares::Node::System root, HeadlessPlatform &plat) {
  uint64_t target = 0;
  {
    std::lock_guard<std::mutex> lock(plat.video_mutex);
    target = plat.video_frames + 1u;
  }
  root->run();
  // Synchronize with ares::Video::Screen worker thread before inspecting rgb/frame dimensions.
  std::unique_lock<std::mutex> lock(plat.video_mutex);
  plat.video_cv.wait_for(lock, std::chrono::seconds(2),
                         [&] { return plat.video_frames >= target; });
}

/** Save battery SRAM into isolated or explicit directory and optionally mirror to a file. */
static void flush_and_dump_sram(ares::Node::System root, HeadlessPlatform &plat,
                                const std::string &dump_path) {
  root->save();
  if (plat.system_pak) plat.system_pak->save(plat.system_pak->location);
  if (plat.game_pak) plat.game_pak->save(plat.game_pak->location);
  if (plat.disc_pak) plat.disc_pak->save(plat.disc_pak->location);

  if (dump_path.empty() || !plat.game_pak || !plat.game_pak->pak) return;
  if (auto fp = plat.game_pak->pak->read("save.ram")) {
    std::ofstream ofs(dump_path, std::ios::binary);
    if (ofs) {
      ofs.write(reinterpret_cast<const char *>(fp->data()),
                static_cast<std::streamsize>(fp->size()));
    }
  }
}

/** Initialize ares + mia, load ROM/disc, connect pads, and boot the emulated system. */
struct EmulatorSession {
  HeadlessPlatform platform;
  ares::Node::System root;
  std::string temp_sram_dir;
  bool loaded = false;

  ~EmulatorSession() {
    if (loaded && root) {
      root->save();
      if (platform.system_pak) platform.system_pak->save(platform.system_pak->location);
      if (platform.game_pak) platform.game_pak->save(platform.game_pak->location);
      if (platform.disc_pak) platform.disc_pak->save(platform.disc_pak->location);
      root->unload();
      root.reset();
    }
    ares::platform = nullptr;
    if (!temp_sram_dir.empty()) {
      std::error_code ec;
      fs::remove_all(temp_sram_dir, ec);
    }
  }

  /** Configure SRAM isolation, load media & BIOS, power on core, and attach controllers. */
  bool open(const std::string &rom_path, const Options &opt) {
    mia::construct();

    std::string home_base;
    if (!opt.sram_dir.empty()) {
      fs::create_directories(opt.sram_dir);
      home_base = fs::absolute(opt.sram_dir).string();
    } else {
      fs::path tmp = fs::temp_directory_path() /
                     ("ares_harness_" + std::to_string(
                       std::chrono::steady_clock::now().time_since_epoch().count()));
      fs::create_directories(tmp);
      temp_sram_dir = tmp.string();
      home_base = temp_sram_dir;
    }
    if (!home_base.empty() && home_base.back() != '/') home_base.push_back('/');
    mia::setHomeLocation([home_base]() -> string { return string{home_base.c_str()}; });
    mia::setSaveLocation([home_base]() -> string { return string{home_base.c_str()}; });

    SystemKind kind = detect_system_kind(rom_path, opt, platform.disc_pak);
    if (kind == SystemKind::MegaCD) {
      platform.game_pak = mia::Medium::create("Mega CD");
    } else if (kind == SystemKind::Mega32X || kind == SystemKind::MegaCD32X) {
      platform.game_pak = mia::Medium::create("Mega 32X");
    } else {
      platform.game_pak = mia::Medium::create("Mega Drive");
    }
    if (!platform.game_pak) {
      std::fprintf(stderr, "ares-harness: failed to create mia medium\n");
      return false;
    }

    LoadResult res = platform.game_pak->load(string{rom_path.c_str()});
    if (res != successful) {
      std::fprintf(stderr, "ares-harness: failed to load ROM '%s' (err=%d)\n",
                   rom_path.c_str(), static_cast<int>(res.result));
      return false;
    }

    // Upgrade MD/32X cart requesting Mega CD hardware when needed.
    bool needs_mcd = (kind == SystemKind::MegaCD || kind == SystemKind::MegaCD32X ||
                      platform.game_pak->pak->attribute("megacd").boolean());
    std::string region_list(platform.game_pak->pak->attribute("region").data());
    std::string region = select_region(region_list, opt.region);

    std::string sys_cfg;
    if (kind == SystemKind::Mega32X || kind == SystemKind::MegaCD32X) {
      if (needs_mcd) {
        platform.system_pak = mia::System::create("Mega CD 32X");
        std::string bios = resolve_cd_bios(opt, region);
        if (bios.empty() || platform.system_pak->load(string{bios.c_str()}) != successful) {
          std::fprintf(stderr, "ares-harness: missing Mega CD BIOS for Mega CD 32X\n");
          return false;
        }
        sys_cfg = "[Sega] Mega CD 32X (" + region + ")";
      } else {
        platform.system_pak = mia::System::create("Mega 32X");
        if (platform.system_pak->load() != successful) {
          std::fprintf(stderr, "ares-harness: failed to load Mega 32X system ROMs\n");
          return false;
        }
        sys_cfg = "[Sega] Mega 32X (" + region + ")";
      }
    } else if (kind == SystemKind::MegaCD || needs_mcd) {
      platform.system_pak = mia::System::create("Mega CD");
      std::string bios = resolve_cd_bios(opt, region);
      if (bios.empty() || platform.system_pak->load(string{bios.c_str()}) != successful) {
        std::fprintf(stderr, "ares-harness: missing Mega CD BIOS ('%s')\n", bios.c_str());
        return false;
      }
      sys_cfg = "[Sega] Mega CD (" + region + ")";
    } else {
      platform.system_pak = mia::System::create("Mega Drive");
      if (platform.system_pak->load() != successful) {
        std::fprintf(stderr, "ares-harness: failed to load Mega Drive system ROMs\n");
        return false;
      }
      sys_cfg = "[Sega] Mega Drive (" + region + ")";
    }

    ares::platform = &platform;
    md_set_option("Recompiler", opt.use_recompiler ? "true" : "false");
    md_set_option("TMSS", opt.use_tmss ? "true" : "false");

    if (!md_load_system(root, sys_cfg.c_str())) {
      std::fprintf(stderr, "ares-harness: ares::MegaDrive::load(%s) failed\n", sys_cfg.c_str());
      return false;
    }

    if (kind != SystemKind::MegaCD) {
      if (auto port = root->find<ares::Node::Port>("Cartridge Slot")) {
        port->allocate();
        port->connect();
      }
    } else {
      if (auto port = root->find<ares::Node::Port>("Mega CD/Disc Tray")) {
        port->allocate();
        port->connect();
      }
    }

    const char *pad_name = (opt.pad_type == "control") ? "Control Pad" : "Fighting Pad";
    if (auto port1 = root->find<ares::Node::Port>("Controller Port 1")) {
      port1->allocate(pad_name);
      port1->connect();
    }
    if (auto port2 = root->find<ares::Node::Port>("Controller Port 2")) {
      port2->allocate(pad_name);
      port2->connect();
    }

    if (!platform.open_wav(opt.wav_path)) return false;

    root->power();
    loaded = true;
    return true;
  }
};

/** Print script audiostat line using energy accumulated since prior checkpoint. */
static void print_audiostat(HeadlessPlatform &plat, const std::string &label) {
  uint64_t avg = plat.interval_samples ? (plat.interval_energy / plat.interval_samples) : 0u;
  std::printf("audiostat@%s samples=%llu avg_energy=%llu\n",
              label.c_str(),
              static_cast<unsigned long long>(plat.interval_samples),
              static_cast<unsigned long long>(avg));
  plat.interval_samples = 0;
  plat.interval_energy = 0;
}

/** Execute multi-line file script (Diabolical32X tests & benchmarks). */
static int run_script_file(EmulatorSession &sess, const std::string &script_path,
                           const std::string &outdir, const Options &opt) {
  fs::create_directories(outdir);
  std::ifstream sf(script_path);
  if (!sf) {
    std::perror(script_path.c_str());
    return 2;
  }

  std::string line;
  unsigned cur_port = 0;
  while (std::getline(sf, line)) {
    size_t hash = line.find('#');
    if (hash != std::string::npos) line.resize(hash);
    line = trim_ws(line);
    if (line.empty()) continue;

    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    if (cmd == "run") {
      int frames = 0;
      if (!(iss >> frames)) {
        std::fprintf(stderr, "bad script line: %s\n", line.c_str());
        return 4;
      }
      for (int i = 0; i < frames; ++i) step_frame(sess.root, sess.platform);
    } else if (cmd == "press") {
      std::string btn;
      int frames = 0;
      int bit = -1;
      if (!(iss >> btn >> frames) || (bit = lookup_button_bit(btn)) < 0) {
        std::fprintf(stderr, "bad press line: %s\n", line.c_str());
        return 4;
      }
      sess.platform.pad[cur_port] |= (1u << bit);
      for (int i = 0; i < frames; ++i) step_frame(sess.root, sess.platform);
      sess.platform.pad[cur_port] &= ~(1u << bit);
    } else if (cmd == "hold") {
      std::string btn;
      int bit = -1;
      if (!(iss >> btn) || (bit = lookup_button_bit(btn)) < 0) {
        std::fprintf(stderr, "bad hold line: %s\n", line.c_str());
        return 4;
      }
      sess.platform.pad[cur_port] |= (1u << bit);
    } else if (cmd == "release") {
      std::string btn;
      int bit = -1;
      if (!(iss >> btn) || (bit = lookup_button_bit(btn)) < 0) {
        std::fprintf(stderr, "bad release line: %s\n", line.c_str());
        return 4;
      }
      sess.platform.pad[cur_port] &= ~(1u << bit);
    } else if (cmd == "port") {
      int p = 0;
      if (!(iss >> p)) {
        std::fprintf(stderr, "bad port line: %s\n", line.c_str());
        return 4;
      }
      cur_port = static_cast<unsigned>(p) & 1u;
    } else if (cmd == "shot") {
      std::string name;
      if (!(iss >> name)) {
        std::fprintf(stderr, "bad shot line: %s\n", line.c_str());
        return 4;
      }
      bool ok = (opt.format == ShotFormat::PPM)
                  ? write_ppm(sess.platform, outdir + "/" + name + ".ppm")
                  : write_bmp(sess.platform, outdir + "/" + name + ".bmp");
      if (!ok) return 3;
    } else if (cmd == "ram") {
      std::string name;
      if (!(iss >> name)) {
        std::fprintf(stderr, "bad ram line: %s\n", line.c_str());
        return 4;
      }
      if (!md_dump_m68k_ram((outdir + "/" + name + ".ram").c_str())) return 3;
    } else if (cmd == "sdram") {
      std::string name;
      if (!(iss >> name)) {
        std::fprintf(stderr, "bad sdram line: %s\n", line.c_str());
        return 4;
      }
      FILE *pf = std::fopen((outdir + "/" + name + ".sdram").c_str(), "wb");
      if (!pf) return 3;
      bool ok = md_write_sdram_slice(pf, 0u, 0x40000u);
      std::fclose(pf);
      if (!ok) return 3;
    } else if (cmd == "comm") {
      if (md_is_32x()) {
        uint16_t comm[8] = {};
        md_read_comm(comm);
        std::printf("comm:");
        for (int i = 0; i < 8; ++i) {
          std::printf(" %04X", static_cast<unsigned>(comm[i]));
        }
        std::printf("\n");
      }
    } else if (cmd == "audiostat") {
      std::string label;
      if (!(iss >> label)) label = std::to_string(sess.platform.video_frames);
      print_audiostat(sess.platform, label);
    } else if (cmd == "sample_prof") {
      std::string name, off_str;
      unsigned len = 0;
      int frames = 0, step = 0;
      if (!(iss >> name >> off_str >> len >> frames >> step) || len == 0 || frames <= 0 || step <= 0) {
        std::fprintf(stderr, "bad sample_prof line: %s\n", line.c_str());
        return 4;
      }
      unsigned long off = std::strtoul(off_str.c_str(), nullptr, 16);
      if (off > 0x40000UL || len > 0x40000UL - off) {
        std::fprintf(stderr, "sample_prof offset out of SDRAM bounds: %s\n", line.c_str());
        return 4;
      }
      std::string ppath = outdir + "/" + name + ".bin";
      FILE *pf = std::fopen(ppath.c_str(), "wb");
      if (!pf) {
        std::perror(ppath.c_str());
        return 3;
      }
      for (int i = 0; i < frames; ++i) {
        step_frame(sess.root, sess.platform);
        // Dump struct snapshot every <step> VBlanks.
        if (((i + 1) % step) == 0) {
          if (!md_write_sdram_slice(pf, static_cast<uint32_t>(off), len)) {
            std::fclose(pf);
            return 3;
          }
        }
      }
      std::fclose(pf);
    } else {
      std::fprintf(stderr, "bad script line: %s\n", line.c_str());
      return 4;
    }
  }

  std::string apath = outdir + "/audio.txt";
  FILE *af = std::fopen(apath.c_str(), "w");
  if (af) {
    std::fprintf(af, "total=%llu\nnonzero=%llu\npeak=%d\nenergy=%llu\n",
                 static_cast<unsigned long long>(sess.platform.audio_samples_total),
                 static_cast<unsigned long long>(sess.platform.audio_samples_nonzero),
                 sess.platform.audio_peak,
                 static_cast<unsigned long long>(sess.platform.audio_energy));
    std::fclose(af);
  }

  flush_and_dump_sram(sess.root, sess.platform, opt.dump_sram);
  return 0;
}

/** Parsed directive item for --inline mode (compatible with run_headless_emulator_test.sh). */
struct InlineRange {
  int f0 = 0;
  int f1 = 0;
  unsigned port = 0;
  int bit = -1;
};

struct InlineMash {
  int f0 = 0;
  int f1 = 0;
  int period = 10;
  unsigned port = 0;
  int b0 = -1;
  int b1 = -1;
};

/** Parse comma-separated inline directives and step through TOTAL_FRAMES. */
static int run_inline_directives(EmulatorSession &sess, int total_frames,
                                 const std::string &spec, const std::string &out_prefix,
                                 const Options &opt) {
  std::vector<InlineRange> holds;
  std::vector<InlineMash> mashes;
  std::vector<int> shots;
  std::vector<int> rams;
  std::vector<int> audiostats;

  if (!spec.empty() && spec != "-") {
    for (const std::string &raw : split_char(spec, ',')) {
      std::string tok = trim_ws(raw);
      if (tok.empty()) continue;
      if (tok.rfind("shot@", 0) == 0) {
        shots.push_back(std::atoi(tok.c_str() + 5));
      } else if (tok.rfind("ram@", 0) == 0) {
        rams.push_back(std::atoi(tok.c_str() + 4));
      } else if (tok.rfind("audiostat@", 0) == 0) {
        audiostats.push_back(std::atoi(tok.c_str() + 10));
      } else if (tok.rfind("mash@", 0) == 0) {
        std::vector<std::string> parts = split_char(tok.substr(5), ':');
        if (parts.size() != 3) {
          std::fprintf(stderr, "bad mash directive: %s\n", tok.c_str());
          return 4;
        }
        std::vector<std::string> rng = split_char(parts[0], '-');
        std::vector<std::string> btns = split_char(parts[2], '+');
        if (rng.size() != 2 || btns.size() != 2) {
          std::fprintf(stderr, "bad mash directive: %s\n", tok.c_str());
          return 4;
        }
        InlineMash m;
        m.f0 = std::atoi(rng[0].c_str());
        m.f1 = std::atoi(rng[1].c_str());
        m.period = std::max(2, std::atoi(parts[1].c_str()));
        std::string a = btns[0], b = btns[1];
        if (a.rfind("p2.", 0) == 0) { m.port = 1; a = a.substr(3); }
        if (b.rfind("p2.", 0) == 0) { m.port = 1; b = b.substr(3); }
        m.b0 = lookup_button_bit(a);
        m.b1 = lookup_button_bit(b);
        if (m.b0 < 0 || m.b1 < 0) {
          std::fprintf(stderr, "unknown mash button in %s\n", tok.c_str());
          return 4;
        }
        mashes.push_back(m);
      } else {
        std::vector<std::string> parts = split_char(tok, ':');
        if (parts.size() != 2) {
          std::fprintf(stderr, "bad directive: %s\n", tok.c_str());
          return 4;
        }
        std::vector<std::string> rng = split_char(parts[0], '-');
        if (rng.size() != 2) {
          std::fprintf(stderr, "bad range directive: %s\n", tok.c_str());
          return 4;
        }
        InlineRange hr;
        hr.f0 = std::atoi(rng[0].c_str());
        hr.f1 = std::atoi(rng[1].c_str());
        std::string bname = parts[1];
        if (bname.rfind("p2.", 0) == 0) {
          hr.port = 1;
          bname = bname.substr(3);
        }
        hr.bit = lookup_button_bit(bname);
        if (hr.bit < 0) {
          std::fprintf(stderr, "unknown button in %s\n", tok.c_str());
          return 4;
        }
        holds.push_back(hr);
      }
    }
  }

  for (int f = 0; f <= total_frames; ++f) {
    sess.platform.pad[0] = 0;
    sess.platform.pad[1] = 0;
    for (const auto &hr : holds) {
      if (f >= hr.f0 && f <= hr.f1) sess.platform.pad[hr.port] |= (1u << hr.bit);
    }
    for (const auto &m : mashes) {
      if (f >= m.f0 && f <= m.f1) {
        int phase = (f - m.f0) % m.period;
        int b = (phase < m.period / 2) ? m.b0 : m.b1;
        sess.platform.pad[m.port] |= (1u << b);
      }
    }

    step_frame(sess.root, sess.platform);

    for (int sf : shots) {
      if (f == sf) {
        std::string ext = (opt.format == ShotFormat::BMP) ? ".bmp" : ".ppm";
        std::string path = out_prefix + std::to_string(f) + ext;
        bool ok = (opt.format == ShotFormat::BMP)
                    ? write_bmp(sess.platform, path)
                    : write_ppm(sess.platform, path);
        if (!ok) return 3;
      }
    }
    for (int rf : rams) {
      if (f == rf) {
        if (!md_dump_m68k_ram((out_prefix + std::to_string(f) + ".ram").c_str())) return 3;
      }
    }
    for (int af : audiostats) {
      if (f == af) print_audiostat(sess.platform, std::to_string(f));
    }
  }

  flush_and_dump_sram(sess.root, sess.platform,
                      opt.dump_sram.empty() ? (out_prefix + "sram.bin") : opt.dump_sram);
  return 0;
}

/** Verify 32X dual-SH2 boot + non-black screen + active COMM register activity. */
static int run_comm_check(EmulatorSession &sess, int frames) {
  for (int i = 0; i < frames; ++i) step_frame(sess.root, sess.platform);

  unsigned lit = 0;
  size_t px_count = sess.platform.rgb.size() / 3u;
  for (size_t i = 0; i < sess.platform.rgb.size(); i += 3u) {
    if (sess.platform.rgb[i] > 8 || sess.platform.rgb[i + 1] > 8 || sess.platform.rgb[i + 2] > 8) {
      ++lit;
    }
  }
  double lit_ratio = px_count ? (static_cast<double>(lit) / static_cast<double>(px_count)) : 0.0;
  std::printf("comm-check %ux%u lit=%.3f", sess.platform.frame_w, sess.platform.frame_h, lit_ratio);
  if (md_is_32x()) {
    uint16_t comm[8] = {};
    md_read_comm(comm);
    std::printf(" comm=[");
    for (int i = 0; i < 8; ++i) {
      if (i > 0) std::printf(" ");
      std::printf("%04X", static_cast<unsigned>(comm[i]));
    }
    std::printf("]");
  }
  std::printf("\n");
  if (lit_ratio < 0.01) {
    std::fprintf(stderr, "comm-check FAIL: black/blank screen after %d frames\n", frames);
    return 1;
  }
  return 0;
}

static void print_usage(const char *argv0) {
  std::fprintf(
    stderr,
    "Usage:\n"
    "  %s [options] <rom> <script.txt> <outdir>\n"
    "  %s --inline [options] <rom> <total_frames> <directives> <out_prefix>\n"
    "  %s --comm-check [options] <rom.32x> [frames]\n\n"
    "Options:\n"
    "  --system <auto|32x|md|mcd|mcd32x>  Force emulated system (default: auto)\n"
    "  --region <NTSC-U|NTSC-J|PAL>       Override console region\n"
    "  --pad <fighting|control>           Controller type (default: fighting)\n"
    "  --recompiler                       Enable SH-2 JIT recompiler (default: interpreter)\n"
    "  --tmss                             Enable Mega Drive TMSS boot ROM\n"
    "  --bios <path>                      Sega CD / Mega CD BIOS path\n"
    "  --sram-dir <dir>                   Persistent SRAM directory (default: isolated temp)\n"
    "  --dump-sram <path>                 Write raw save.ram snapshot on exit\n"
    "  --wav <path>                       Write stereo 48 kHz WAV audio\n"
    "  --format <bmp|ppm>                 Screenshot format (default: bmp, or ppm in --inline)\n",
    argv0, argv0, argv0);
}

}  // namespace

// Satisfy unreferenced nall::main(Arguments) pulled from nall OBJECT library;
// standard ::main(int, char**) below remains the process entry point.
namespace nall { auto main(Arguments) -> void {} }

int main(int argc, char **argv) {
  Options opt;
  if (const char *env_recomp = std::getenv("ARES_RECOMPILER")) {
    if (std::strcmp(env_recomp, "1") == 0 || std::strcmp(env_recomp, "true") == 0) {
      opt.use_recompiler = true;
    }
  }
  if (const char *env_wav = std::getenv("HARNESS_WAV")) {
    if (env_wav[0] != '\0') opt.wav_path = env_wav;
  }

  std::vector<std::string> pos;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--help" || a == "-h") {
      print_usage(argv[0]);
      return 0;
    } else if (a == "--inline") {
      opt.inline_mode = true;
      if (!opt.format_explicit) opt.format = ShotFormat::PPM;
    } else if (a == "--comm-check") {
      opt.comm_check = true;
    } else if (a == "--recompiler") {
      opt.use_recompiler = true;
    } else if (a == "--tmss") {
      opt.use_tmss = true;
    } else if (a == "--system" && i + 1 < argc) {
      std::string v = to_lower_ascii(argv[++i]);
      if (v == "auto") opt.system = SystemKind::Auto;
      else if (v == "32x" || v == "mega32x") opt.system = SystemKind::Mega32X;
      else if (v == "md" || v == "genesis") opt.system = SystemKind::MegaDrive;
      else if (v == "mcd" || v == "scd") opt.system = SystemKind::MegaCD;
      else if (v == "mcd32x") opt.system = SystemKind::MegaCD32X;
      else {
        std::fprintf(stderr, "unknown system '%s'\n", v.c_str());
        return 1;
      }
    } else if (a == "--region" && i + 1 < argc) {
      opt.region = argv[++i];
    } else if (a == "--pad" && i + 1 < argc) {
      opt.pad_type = to_lower_ascii(argv[++i]);
    } else if (a == "--bios" && i + 1 < argc) {
      opt.bios_path = argv[++i];
    } else if (a == "--sram-dir" && i + 1 < argc) {
      opt.sram_dir = argv[++i];
    } else if (a == "--dump-sram" && i + 1 < argc) {
      opt.dump_sram = argv[++i];
    } else if (a == "--wav" && i + 1 < argc) {
      opt.wav_path = argv[++i];
    } else if (a == "--format" && i + 1 < argc) {
      std::string f = to_lower_ascii(argv[++i]);
      opt.format_explicit = true;
      if (f == "bmp") opt.format = ShotFormat::BMP;
      else if (f == "ppm") opt.format = ShotFormat::PPM;
      else {
        std::fprintf(stderr, "unknown format '%s'\n", f.c_str());
        return 1;
      }
    } else if (!a.empty() && a[0] == '-') {
      std::fprintf(stderr, "unknown option '%s'\n", a.c_str());
      return 1;
    } else {
      pos.push_back(a);
    }
  }

  if (opt.comm_check) {
    if (pos.empty() || pos.size() > 2) {
      print_usage(argv[0]);
      return 1;
    }
    int frames = (pos.size() == 2) ? std::atoi(pos[1].c_str()) : 180;
    if (frames <= 0) frames = 180;
    EmulatorSession sess;
    if (!sess.open(pos[0], opt)) return 2;
    return run_comm_check(sess, frames);
  }

  if (opt.inline_mode) {
    if (pos.size() != 4) {
      print_usage(argv[0]);
      return 1;
    }
    int total_frames = std::atoi(pos[1].c_str());
    EmulatorSession sess;
    if (!sess.open(pos[0], opt)) return 2;
    return run_inline_directives(sess, total_frames, pos[2], pos[3], opt);
  }

  if (pos.size() != 3) {
    print_usage(argv[0]);
    return 1;
  }

  EmulatorSession sess;
  if (!sess.open(pos[0], opt)) return 2;
  return run_script_file(sess, pos[1], pos[2], opt);
}
