#include "common.hpp"
#include <QCryptographicHash>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <fstream>
#include <sstream>
#include <sys/resource.h>
#include <unistd.h>

namespace miner {
const Path root =
    fs::canonical(Path(__FILE__)).parent_path().parent_path().parent_path();
const Path main_repo = root.parent_path() / "unllm-upgrade";
const Path volume = "/run/media/whisper/UNLLM_CORPUS";
const Path runs = volume / "unllm/experiments/brick-miner";

void check(bool value, const std::string &message) {
  if (!value)
    throw std::runtime_error(message);
}
std::string bytes(const Path &file) {
  std::ifstream in(file, std::ios::binary);
  check(bool(in), "cannot read " + file.string());
  std::ostringstream contents;
  contents << in.rdbuf();
  check(!in.bad(), "failed reading " + file.string());
  return contents.str();
}
void save(const Path &file, const std::string &data) {
  if (!file.parent_path().empty())
    fs::create_directories(file.parent_path());
  std::ofstream out(file, std::ios::binary);
  out.write(data.data(), data.size());
  out.close();
  check(bool(out), "cannot write " + file.string());
}
J parse(const std::string &s) {
  Json::CharReaderBuilder b;
  b["collectComments"] = false;
  b["failIfExtra"] = true;
  J result;
  std::string msg;
  std::unique_ptr<Json::CharReader> parser(b.newCharReader());
  check(parser->parse(s.data(), s.data() + s.size(), &result, &msg),
        "invalid JSON: " + msg);
  return result;
}
J load(const Path &file) { return parse(bytes(file)); }
J object(std::initializer_list<std::pair<std::string, J>> values) {
  J result(Json::objectValue);
  for (const auto &[k, v] : values)
    result[k] = v;
  return result;
}
J array(std::initializer_list<J> values) {
  J a(Json::arrayValue);
  for (const auto &v : values)
    a.append(v);
  return a;
}
J string_array(const Strings &values) {
  J a(Json::arrayValue);
  for (const auto &v : values)
    a.append(v);
  return a;
}
Strings strings(const J &values) {
  Strings v;
  for (const auto &item : values)
    v.push_back(item.asString());
  return v;
}
std::string dump(const J &v, bool pretty, bool ascii) {
  Json::StreamWriterBuilder w;
  w["indentation"] = "";
  w["emitUTF8"] = !ascii;
  w["commentStyle"] = "None";
  std::string out;
  std::function<void(const J &, int)> render = [&](const J &n, int depth) {
    if (!n.isObject() && !n.isArray()) {
      out += Json::writeString(w, n);
      return;
    }
    out += n.isArray() ? '[' : '{';
    unsigned i = 0;
    for (auto it = n.begin(); it != n.end(); ++it, ++i) {
      if (i)
        out += pretty ? "," : ", ";
      if (pretty)
        out += "\n" + std::string(2 * depth + 2, ' ');
      if (n.isObject())
        out += Json::writeString(w, it.key()) + ": ";
      render(*it, depth + 1);
    }
    if (pretty && i)
      out += "\n" + std::string(2 * depth, ' ');
    out += n.isArray() ? ']' : '}';
  };
  render(v, 0);
  if (pretty)
    out += '\n';
  return out;
}
void write(const Path &file, const J &value) { save(file, dump(value)); }
std::string sha(const std::string &data) {
  return QCryptographicHash::hash(QByteArrayView(data),
                                  QCryptographicHash::Sha256)
      .toHex()
      .toStdString();
}
std::string join(const Strings &parts, const std::string &d) {
  std::string s;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i)
      s += d;
    s += parts[i];
  }
  return s;
}
std::string replace(std::string s, const std::string &a, const std::string &b) {
  check(!a.empty(), "empty replacement pattern");
  for (size_t pos = 0; (pos = s.find(a, pos)) != std::string::npos;
       pos += b.size())
    s.replace(pos, a.size(), b);
  return s;
}
void append(Strings &a, const Strings &b) {
  a.insert(a.end(), b.begin(), b.end());
}
Path project_file(const Path &base, const std::string &name) {
  check(!Path(name).is_absolute(), "absolute project filename: " + name);
  auto path = fs::canonical(base / name);
  auto rel = path.lexically_relative(fs::canonical(base));
  check(!rel.empty() && *rel.begin() != "..", "outside project: " + name);
  return path;
}
Result execute(const Strings &args, const RunOptions &options) {
  check(!args.empty(), "missing command");
  QProcess proc;
  proc.setProgram(QString::fromStdString(args.front()));
  QStringList argv;
  for (size_t i = 1; i < args.size(); ++i)
    argv.append(QString::fromStdString(args[i]));
  proc.setArguments(argv);
  if (!options.cwd.empty())
    proc.setWorkingDirectory(QString::fromStdString(options.cwd.string()));
  auto env = QProcessEnvironment::systemEnvironment();
  for (const auto &[k, v] : options.environment)
    env.insert(QString::fromStdString(k), QString::fromStdString(v));
  proc.setProcessEnvironment(env);
  if (!options.output.empty())
    proc.setStandardOutputFile(QString::fromStdString(options.output.string()));
  if (!options.errors.empty())
    proc.setStandardErrorFile(QString::fromStdString(options.errors.string()));
  if (options.probe_limits)
    proc.setChildProcessModifier([] {
      for (auto [kind, limit] :
           {std::pair{RLIMIT_AS, 512 * 1024 * 1024}, std::pair{RLIMIT_CPU, 3},
            std::pair{RLIMIT_FSIZE, 4 * 1024 * 1024},
            std::pair{RLIMIT_CORE, 0}}) {
        rlimit r{rlim_t(limit), rlim_t(limit)};
        if (setrlimit(kind, &r))
          _exit(125);
      }
    });
  proc.start();
  check(proc.waitForStarted(),
        args[0] + ": " + proc.errorString().toStdString());
  if (!options.input.empty())
    proc.write(options.input.data(), qint64(options.input.size()));
  proc.closeWriteChannel();
  bool finished = proc.waitForFinished(options.seconds * 1000);
  if (!finished) {
    proc.kill();
    proc.waitForFinished();
  }
  return {finished && proc.exitStatus() == QProcess::NormalExit
              ? proc.exitCode()
              : -1,
          proc.readAllStandardOutput().toStdString(),
          proc.readAllStandardError().toStdString()};
}
std::string run(const Strings &args, const RunOptions &options) {
  auto result = execute(args, options);
  check(!result.status, args[0] + " failed (" + std::to_string(result.status) +
                            "): " + result.err + result.out);
  return result.out;
}
Strings sandbox(const Path &exe, const Path &writable) {
  Strings args = {"bwrap",
                  "--die-with-parent",
                  "--unshare-all",
                  "--new-session",
                  "--clearenv",
                  "--setenv",
                  "LC_ALL",
                  "C",
                  "--ro-bind",
                  "/usr",
                  "/usr",
                  "--symlink",
                  "usr/lib",
                  "/lib",
                  "--symlink",
                  "usr/lib",
                  "/lib64",
                  "--dev",
                  "/dev",
                  "--tmpfs",
                  "/tmp",
                  "--ro-bind",
                  exe,
                  "/probe"};
  if (!writable.empty())
    append(args, {"--bind", writable, "/case", "--chdir", "/case"});
  args.push_back("/probe");
  return args;
}
Strings command_args(const J &entry) {
  if (entry["arguments"].isArray())
    return strings(entry["arguments"]);
  // Compilation databases permit a quoted command string. Split quotes and
  // escapes only.
  auto s = entry["command"].asString();
  Strings result;
  std::string word;
  char quote = 0;
  bool started = false;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (!quote && std::isspace(static_cast<unsigned char>(c))) {
      if (started) {
        result.push_back(word);
        word.clear();
        started = false;
      }
      continue;
    }
    started = true;
    if (c == quote) {
      quote = 0;
      continue;
    }
    if (!quote && (c == '\'' || c == '"')) {
      quote = c;
      continue;
    }
    if (c == '\\' && quote != '\'') {
      check(i + 1 < s.size(), "trailing escape in compile command");
      if (!quote || s[i + 1] == '"' || s[i + 1] == '\\')
        c = s[++i];
    }
    word += c;
  }
  check(!quote, "unterminated quoted compile command");
  if (started)
    result.push_back(word);
  return result;
}
void check_corpus(const Path &target) {
  run({"mountpoint", "-q", volume});
  check(run({"findmnt", "-n", "-o", "UUID", "--target", volume}) ==
            "1f243ff1-6e8f-4562-af7c-f6edf0bf3300\n",
        "unexpected corpus device");
  auto relative =
      fs::weakly_canonical(target).lexically_relative(fs::canonical(runs));
  check(!relative.empty() && *relative.begin() != ".." && relative != ".",
        "outside corpus experiment workspace: " + target.string());
}
Path fresh_run(const std::string &prefix) {
  auto pattern = (runs / (prefix + "-XXXXXXXX")).string();
  check_corpus(pattern);
  check(mkdtemp(pattern.data()) != nullptr,
        "cannot allocate corpus run directory");
  return pattern;
}
} // namespace miner
