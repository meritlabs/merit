#ifndef QRUTIL_H
#define QRUTIL_H

#include <QImage>
#include <QString>
#include <qrencode.h>

namespace qrutil
{
    QImage encodeQString(QString inputString)
    {
        QRcode *code = QRcode_encodeString(inputString.toUtf8().constData(), 0, QR_ECLEVEL_L, QR_MODE_8, 1);
        if (!code)
        {
            return QImage();
        }
        QImage qrImage = QImage(code->width + 8, code->width + 8, QImage::Format_RGB32);
        qrImage.fill(0xffffff);
        unsigned char *p = code->data;
        for (int y = 0; y < code->width; y++)
        {
            for (int x = 0; x < code->width; x++)
            {
                qrImage.setPixel(x + 4, y + 4, ((*p & 1) ? 0x0 : 0xffffff));
                p++;
            }
        }
        QRcode_free(code);

        return qrImage;
    }
}

#endif