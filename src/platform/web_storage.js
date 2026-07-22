// Mount durable browser storage before C main() runs. Emscripten's default
// MEMFS disappears on refresh; IDBFS mirrors /solutions to IndexedDB.
Module.preRun = Module.preRun || [];
Module.preRun.push(function () {
    try {
        FS.mkdir("/solutions");
    } catch (error) {
        if (error.errno !== 20) throw error; // EEXIST
    }

    FS.mount(IDBFS, {}, "/solutions");
    addRunDependency("hex-magical-solutions");
    FS.syncfs(true, function (error) {
        if (error) {
            console.error("SOLUTION: failed to load browser saves", error);
        }
        removeRunDependency("hex-magical-solutions");
    });
});
