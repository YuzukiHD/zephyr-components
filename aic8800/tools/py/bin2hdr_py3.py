import sys
from os.path import isfile, dirname, basename, splitext
import binascii
import codecs

def bin2hdr(binf, hdrf, bytew):
    fh_bin = open(binf, 'rb')
    fh_hdr = open(hdrf, 'wb')
    filename = basename(hdrf).split(".")[0]
    fh_hdr.write(('#ifndef __' + filename.upper() + '_H__\n').encode())
    fh_hdr.write(('#define __' + filename.upper() + '_H__\n\n').encode())
    fh_hdr.write(('#include <stdint.h>\n\n').encode())
    fh_hdr.write(('static const uint32_t ' +  filename + '[] = {\n').encode())
    try:
        while True:
            chunk = fh_bin.read(bytew)
            if not chunk:
                break
            hexstr = binascii.b2a_hex(chunk)
            if bytew > 1:
                hexstr = codecs.decode(hexstr, 'hex')[::-1]
                hexstr = codecs.encode(hexstr, 'hex_codec')
                hexstr = codecs.decode(hexstr, 'ascii')
            fh_hdr.write(('    0x' + hexstr + ',\n').encode())
    finally:
        fh_hdr.write(('};\n').encode())
        fh_hdr.write(('#endif\n').encode())
        fh_hdr.close()
        fh_bin.close()

if __name__ == '__main__':
    bin_path = None
    if len(sys.argv) == 2 and isfile(sys.argv[1]):
        bin_path = sys.argv[1]
        out_path = splitext(bin_path)[0] + '.h'
        bin2hdr(bin_path, out_path, 4)
    else:
        print ('Usage: ' + sys.argv[0] + ' [binary file]')
