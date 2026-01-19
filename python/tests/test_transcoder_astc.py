from basisu_py import Transcoder
from astc_writer import write_astc_file

# Load a .ktx2
data = open("input.ktx2", "rb").read()
t = Transcoder()

# Transcode to ASTC
h = t.open(data)
bw = t.get_block_width(h)   # or basis_get_block_width(astc_tfmt)
bh = t.get_block_height(h)
tfmt = t.basis_get_transcoder_texture_format_from_basis_tex_format(
    t.get_basis_tex_format(h)
)

blocks = t.transcode_tfmt(data, tfmt)
write_astc_file("output.astc", blocks, bw, bh, t.get_width(h), t.get_height(h))
t.close(h)
