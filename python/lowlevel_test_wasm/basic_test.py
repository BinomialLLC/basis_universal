import wasmtime
import ctypes

# --- Engine ---
engine = wasmtime.Engine()

# --- Store ---
store = wasmtime.Store(engine)

# --- WASI config ---
wasi = wasmtime.WasiConfig()
wasi.argv = ["basisu_module_st"]
wasi.inherit_stdout()   # <-- tell WASI to use the host stdout
wasi.inherit_stderr()
store.set_wasi(wasi)

# --- Load module ---
module = wasmtime.Module.from_file(engine, "basisu_py/wasm/basisu_module_st.wasm")

# --- Linker + WASI ---
linker = wasmtime.Linker(engine)
linker.define_wasi()

# --- Instantiate ---
instance = linker.instantiate(store, module)
print("Single-threaded WASM instantiated OK")

# --- Exports ---
exports = instance.exports(store)

get_version = exports["bu_get_version"]
alloc       = exports["bu_alloc"]
free        = exports["bu_free"]
memory      = exports["memory"]

# --- Version ---
version = get_version(store)
print("Version =", version)

# --- Alloc ---
ptr = alloc(store, 64)
print("Allocated ptr =", ptr)

# --- Access WASM memory properly ---
data_len = memory.data_len(store)
raw_ptr  = memory.data_ptr(store)            # ctypes pointer
addr     = ctypes.addressof(raw_ptr.contents)  # convert to integer pointer

# Create a byte array view into WASM memory
buf = (ctypes.c_ubyte * data_len).from_address(addr)

# Write TEST at allocated ptr
buf[ptr : ptr + 4] = b"TEST"
print("Wrote TEST into WASM memory.")

# --- Free ---
free(store, ptr)
print("Memory free OK.")
