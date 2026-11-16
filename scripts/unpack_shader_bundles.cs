// Offline, read-only adapter to the separately installed AnimeStudio VFS reader.
using System.Collections;
using System.Reflection;
using System.Runtime.Loader;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;

if (args.Length != 4) throw new ArgumentException("reader directory, bundles, output, project build root required");
var root = Path.GetFullPath(args[0]);
var output = Path.GetFullPath(args[2]);
var allowed = Path.GetFullPath(args[3]).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
if (!output.StartsWith(allowed, StringComparison.OrdinalIgnoreCase)) throw new Exception("Output escaped project build");
Directory.CreateDirectory(output);
AssemblyLoadContext.Default.Resolving += (context, name) => {
    var file = Path.Combine(root, name.Name + ".dll");
    return File.Exists(file) ? context.LoadFromAssemblyPath(file) : null;
};
AssemblyLoadContext.Default.ResolvingUnmanagedDll += (assembly, name) => {
    foreach (var candidate in new[] {name, name + ".dll"}) {
        var file = Path.Combine(root, Path.GetFileName(candidate));
        if (File.Exists(file) && NativeLibrary.TryLoad(file, out var handle)) return handle;
    }
    return IntPtr.Zero;
};
var readerLibrary = Path.Combine(root, "AnimeStudio.dll");
var lib = AssemblyLoadContext.Default.LoadFromAssemblyPath(readerLibrary);
var logger = DispatchProxy.Create(lib.GetType("AnimeStudio.ILogger", true), typeof(ReadLog));
var loggerType = lib.GetType("AnimeStudio.Logger", true);
loggerType.GetField("Default").SetValue(null, logger);
loggerType.GetProperty("Flags").SetValue(null, Enum.ToObject(lib.GetType("AnimeStudio.LoggerEvent", true), 24));
var readerType = lib.GetType("AnimeStudio.FileReader", true);
var vfsType = lib.GetType("AnimeStudio.VFSFile", true);
var game = Enum.Parse(lib.GetType("AnimeStudio.GameType", true), "ArknightsEndfield");
var reports = new List<object>();
foreach (var path in Directory.EnumerateFiles(args[1], "*.ab").OrderBy(p => p, StringComparer.Ordinal)) {
    var stem = Path.GetFileNameWithoutExtension(path);
    if (!Regex.IsMatch(stem, "^[0-9a-fA-F]+$")) throw new Exception("Expected content-derived bundle filename");
    var prefix = Path.Combine(output, stem);
    using var reader = (IDisposable)Activator.CreateInstance(readerType, new object[] {path});
    var vfs = Activator.CreateInstance(vfsType, new object[] {reader, prefix, game});
    if (((ReadLog)logger).Warnings != 0) throw new Exception("VFS reader reported a warning/error");
    var files = (IEnumerable)vfsType.GetField("fileList").GetValue(vfs);
    var index = 0;
    foreach (var file in files) {
        var type = file.GetType();
        var name = (string)type.GetField("fileName").GetValue(file);
        using var stream = (Stream)type.GetField("stream").GetValue(file);
        if (name.EndsWith(".resS", StringComparison.OrdinalIgnoreCase) || name.EndsWith(".resource", StringComparison.OrdinalIgnoreCase)) continue;
        var target = prefix + "-" + index++ + ".assets";
        using (var dest = File.Create(target)) stream.CopyTo(dest);
        stream.Position = 0;
        var digest = Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
        reports.Add(new {bundle=Path.GetFileName(path), name, bytes=stream.Length, target, sha256=digest});
        Console.WriteLine(Path.GetFileName(path) + " " + name + " " + stream.Length);
    }
}
var manifest = new {reader_sha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(readerLibrary))).ToLowerInvariant(), files=reports};
File.WriteAllText(Path.Combine(output, "manifest.json"), JsonSerializer.Serialize(manifest));

public class ReadLog : DispatchProxy {
    public int Warnings;
    protected override object Invoke(MethodInfo method, object[] args) {
        ++Warnings;
        Console.Error.WriteLine("Reader diagnostic: " + args[0] + " " + args[1]);
        return null;
    }
}
