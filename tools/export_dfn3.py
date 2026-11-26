#!/usr/bin/env python3
"""Writes the weights of DeepFilterNet3 (48 kHz, official ONNX export: enc.onnx, erb_dec.onnx, df_dec.onnx from
DeepFilterNet3_onnx.tar.gz) as one flat file that src/neural/DfnVoice.cpp reads.

    export_dfn3.py <folder with the three .onnx files> <output .bin>

File: "DFN3W001", uint32 count, then per tensor: uint32 name length, name, uint32 rank, uint32 dims[rank],
float32 data (little endian, row-major)."""
import sys, struct, numpy as np, importlib.util, os
here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("dfn3net", os.path.join(here, "dfn3net.py"))
net = importlib.util.module_from_spec(spec); spec.loader.exec_module(net)
W = net.weights(sys.argv[1].rstrip("/") + "/")
with open(sys.argv[2], "wb") as f:
    f.write(b"DFN3W001"); f.write(struct.pack("<I", len(W)))
    for k, v in W.items():
        v = np.ascontiguousarray(v, dtype="<f4"); n = k.encode()
        f.write(struct.pack("<I", len(n))); f.write(n); f.write(struct.pack("<I", v.ndim))
        f.write(struct.pack("<%dI" % v.ndim, *v.shape)); f.write(v.tobytes())
print(len(W), "tensors,", sum(v.size for v in W.values()), "weights")
