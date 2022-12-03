let fs = require("fs")
let child_process = require("child_process")

const buildPath = "build/"

const js = JSON.parse(fs.readFileSync(buildPath + "compile_commands.json", "utf-8"))
const gdb = js[0].command.replace(/ .*/, "").replace(/-gcc$/, "-gdb")
const elfpath = buildPath + "espjd.elf"
const pref = js[0].directory.replace(/[^\/]*\/build/, "")

const args = process.argv.slice(2)
let stack = ""
if (args[0]) {
    stack = fs.readFileSync(args[0], "utf-8")
} else {
    stack = child_process.execSync("pbpaste", {
        encoding: "utf8"
    })
}


const addr = {}
const addrlist = []
let addrgdb = ""

function iterLines(final) {
    for (let line of stack.split(/\n/)) {
        if (final) console.log(line)
        line.replace(/(0x|^|\s)([a-f0-9]{8})([\s,.:]|$)/mig, (_, _p, w) => {
            let k = parseInt(w, 16)
            if (final && addr[k + ""] != "x") {
                console.log("    \x1b[33m0x" + k.toString(16) + ": " + addr[k + ""] + "\x1b[0m")
            }
            if (!addr[k + ""]) {
                addr[k + ""] = "x"
                addrlist.push(k)
                addrgdb += `info line *${k}\n`
            }
        })
    }
}
iterLines()
fs.writeFileSync("build/addr.gdb", addrgdb, "utf-8")
const res = child_process.spawnSync(gdb, [elfpath,
    "--quiet", "--batch", "--command=build/addr.gdb"
], { encoding: "utf-8" })

const lines = res.stdout.split(/\n/)
for (let i = 0; i < addrlist.length; ++i) {
    if (!lines[i]) {
        console.log("missing?", res.stderr, res.stdout)
        break
    }
    const k = addrlist[i] + ""
    let m = /No line number information available for address 0x[a-f0-9]+ (<.*>)/.exec(lines[i])
    if (m) {
        addr[k] = m[1]
        continue
    }

    if (lines[i].startsWith('No line number information available'))
        continue

    m = /Line (\d+) of "([^"]*)"/.exec(lines[i])
    if (m) {
        addr[k] = m[2].replace(pref, "") + ":" + m[1]
        continue
    }

    console.log("WHOOPS", k, lines[i])
}
iterLines(1)

