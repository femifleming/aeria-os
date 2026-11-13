// AERIA Flasher: pick a release Claude made, plug the Octatrack in, flash. The work is done by flasher.py next to
// this app's source (it backs up the saved state, tries the firmware from memory first, then writes and verifies).
import SwiftUI
import AppKit

let root = NSString(string: "~/octa/aeria-os").expandingTildeInPath
let python = NSString(string: "~/.cache/modwerk-upstream/venv/bin/python").expandingTildeInPath
let engine = root + "/flasher/flasher.py"

struct Release: Identifiable, Hashable { let id: String; let name: String; let made: String; let tested: Bool; let notes: String }

final class Model: ObservableObject {
    @Published var releases: [Release] = []
    @Published var selected: Release.ID?
    @Published var unit = "Not checked yet"
    @Published var unitOK = false
    @Published var log: [String] = []
    @Published var step = 0.0
    @Published var busy = false
    @Published var result: String?
    @Published var failed = false

    func run(_ args: [String], onLine: @escaping (String) -> Void, done: @escaping (Int32) -> Void) {
        let p = Process(); p.executableURL = URL(fileURLWithPath: python); p.arguments = ["-B", engine] + args
        let pipe = Pipe(); p.standardOutput = pipe; p.standardError = pipe
        var buf = Data()
        pipe.fileHandleForReading.readabilityHandler = { h in
            let d = h.availableData; if d.isEmpty { return }
            buf.append(d)
            while let r = buf.range(of: Data([10])) {
                let line = String(decoding: buf.subdata(in: 0..<r.lowerBound), as: UTF8.self); buf.removeSubrange(0..<r.upperBound)
                DispatchQueue.main.async { onLine(line) }
            }
        }
        p.terminationHandler = { pr in DispatchQueue.main.async { pipe.fileHandleForReading.readabilityHandler = nil; done(pr.terminationStatus) } }
        do { try p.run() } catch { onLine("FAIL Could not start the flasher engine: \(error.localizedDescription)"); done(1) }
    }
    func refresh() {
        var rs: [Release] = []
        run(["list"], onLine: { l in
            guard l.hasPrefix("RELEASE ") else { return }
            let f = l.dropFirst(8).components(separatedBy: "\t")
            if f.count >= 5 { rs.append(Release(id: f[0], name: f[1], made: f[2], tested: f[3] == "yes", notes: f[4])) }
        }, done: { _ in self.releases = rs; if self.selected == nil { self.selected = rs.first?.id } })
    }
    func checkUnit() {
        unit = "Looking…"; unitOK = false
        run(["unit"], onLine: { l in
            if l.hasPrefix("UNIT ") { self.unit = String(l.dropFirst(5)); self.unitOK = true }
            if l.hasPrefix("FAIL ") { self.unit = String(l.dropFirst(5)) }
        }, done: { _ in })
    }
    func flash() {
        guard let r = releases.first(where: { $0.id == selected }) else { return }
        busy = true; log = []; step = 0; result = nil; failed = false
        run(["flash", r.id], onLine: { l in
            if l.hasPrefix("STEP ") {
                let rest = l.dropFirst(5); let parts = rest.split(separator: " ", maxSplits: 1)
                let nm = parts.first?.split(separator: "/") ?? []
                if nm.count == 2, let a = Double(nm[0]), let b = Double(nm[1]) { self.step = a / b }
                self.log.append("● " + (parts.count > 1 ? String(parts[1]) : ""))
            } else if l.hasPrefix("STATUS ") { self.log.append("    " + l.dropFirst(7)) }
            else if l.hasPrefix("OK ") { self.result = String(l.dropFirst(3)); self.failed = false }
            else if l.hasPrefix("FAIL ") { self.result = String(l.dropFirst(5)); self.failed = true }
            else if !l.isEmpty { self.log.append("    " + l) }
        }, done: { code in self.busy = false; if code != 0 && self.result == nil { self.result = "Something went wrong (code \(code))."; self.failed = true }; self.checkUnit() })
    }
    func restoreState() {
        let pnl = NSOpenPanel(); pnl.directoryURL = URL(fileURLWithPath: root + "/backups"); pnl.allowedContentTypes = []; pnl.canChooseDirectories = false
        guard pnl.runModal() == .OK, let u = pnl.url else { return }
        busy = true; log = ["● Putting back \(u.lastPathComponent)"]; result = nil
        run(["restore-state", u.path], onLine: { l in
            if l.hasPrefix("OK ") { self.result = String(l.dropFirst(3)); self.failed = false }
            if l.hasPrefix("FAIL ") { self.result = String(l.dropFirst(5)); self.failed = true }
        }, done: { _ in self.busy = false })
    }
}

struct ContentView: View {
    @StateObject var m = Model()
    @State var confirm = false
    var sel: Release? { m.releases.first(where: { $0.id == m.selected }) }
    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                Text("AERIA Flasher").font(.system(size: 22, weight: .semibold))
                Spacer()
                Circle().fill(m.unitOK ? Color.green : Color.orange).frame(width: 10, height: 10)
                Text(m.unit).font(.callout).lineLimit(2)
                Button("Check Octatrack") { m.checkUnit() }.disabled(m.busy)
            }
            Text("Firmware releases (newest first)").font(.headline)
            List(m.releases, selection: $m.selected) { r in
                VStack(alignment: .leading, spacing: 3) {
                    HStack {
                        Text(r.name).font(.system(.body, design: .monospaced))
                        if r.tested { Text("tested on the unit").font(.caption).padding(.horizontal, 6).background(Color.green.opacity(0.25)).cornerRadius(4) }
                        else { Text("not yet tried on the unit").font(.caption).padding(.horizontal, 6).background(Color.orange.opacity(0.3)).cornerRadius(4) }
                        Spacer(); Text(r.made).font(.caption).foregroundColor(.secondary)
                    }
                    if !r.notes.isEmpty { Text(r.notes).font(.caption).foregroundColor(.secondary).lineLimit(3) }
                }.padding(.vertical, 3).tag(r.id)
            }.frame(minHeight: 170)
            HStack {
                Button { confirm = true } label: { Text("Flash selected firmware").frame(minWidth: 200) }
                    .keyboardShortcut(.defaultAction).disabled(m.busy || sel == nil)
                Button("Refresh list") { m.refresh() }.disabled(m.busy)
                Spacer()
                Button("Put back a saved-state backup…") { m.restoreState() }.disabled(m.busy)
            }
            if m.busy || !m.log.isEmpty {
                ProgressView(value: m.step).progressViewStyle(.linear)
                ScrollView { VStack(alignment: .leading, spacing: 2) { ForEach(Array(m.log.enumerated()), id: \.offset) { Text($0.element).font(.system(.caption, design: .monospaced)) } }.frame(maxWidth: .infinity, alignment: .leading) }.frame(height: 130)
            }
            if let r = m.result {
                Text(r).font(.callout).padding(10).frame(maxWidth: .infinity, alignment: .leading)
                    .background((m.failed ? Color.red : Color.green).opacity(0.18)).cornerRadius(6)
            }
            Text("Plug the Octatrack in by USB with AERIA OS running. The flasher backs up your patterns and settings first, tries the new firmware from memory, and only writes the flash if it starts and draws its screen. Don't unplug while it writes (about a minute).")
                .font(.caption).foregroundColor(.secondary)
        }
        .padding(18).frame(minWidth: 640, minHeight: 560)
        .onAppear { m.refresh(); m.checkUnit() }
        .alert("Flash \(sel?.name ?? "")?", isPresented: $confirm) {
            Button("Flash", role: .destructive) { m.flash() }
            Button("Cancel", role: .cancel) {}
        } message: { Text((sel?.tested ?? false) ? "This build was tried on the unit before it was released." : "This build has NOT been tried on the unit yet. The flasher will still try it from memory first and stop if it does not start.") }
    }
}

@main struct AERIAFlasherApp: App {
    var body: some Scene { WindowGroup("AERIA Flasher") { ContentView() }.windowResizability(.contentSize) }
}
