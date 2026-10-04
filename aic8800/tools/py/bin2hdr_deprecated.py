import sys
from os.path import isfile, dirname
import binascii

def bin2hdr(binf, hdrf, bytew):
    fh_bin = open(binf, 'rb')
    fh_hdr = open(hdrf, 'wb')
    fh_hdr.write('#ifndef __AIC8800D_H__\n')
    fh_hdr.write('#define __AIC8800D_H__\n\n')
    fh_hdr.write('static const unsigned int aic8800d_bin_array[] = {\n')
    try:
        while True:
            chunk = fh_bin.read(bytew)
            if not chunk:
                break
            hexstr = binascii.b2a_hex(chunk)
            if bytew > 1:
                hexstr = hexstr.decode('hex')[::-1].encode('hex_codec')
            fh_hdr.write('    0x' + hexstr + ',\n')
    finally:
        fh_hdr.write('};\n')
        fh_hdr.write('#endif\n')
        fh_hdr.close()
        fh_bin.close()

if __name__ == '__main__':
    bin_path = None
    if len(sys.argv) == 2 and isfile(sys.argv[1]):
        bin_path = sys.argv[1]
        out_path = dirname(bin_path) + '/aic8800d.h'
        bin2hdr(bin_path, out_path, 4)
    else:
        print 'Usage: ' + sys.argv[0] + ' [binary file]'
