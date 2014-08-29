#ifndef CANVAS_H
#define CANVAS_H

#include <QWidget>
#include "../paintingTools/brush/abstractbrush.h"
#include "../misc/layermanager.h"
#include "canvasbackend.h"

class QScrollBar;
typedef QSharedPointer<AbstractBrush> BrushPointer;

class Canvas : public QWidget
{
    Q_OBJECT
public:
    enum AntialiasingMode
    {
        SmallOn = 0x1, //turn on antialiasing when scale factor < 100
        LargeOn = 0x2 //turn on antialiasing when scale factor > 100
    };
    Q_DECLARE_FLAGS(AntialiasingModes, AntialiasingMode)
    explicit Canvas(QWidget *parent = 0);
    ~Canvas();
    QVariantMap brushSettings() const;
    BrushFeature brushFeatures() const;
    QString currentLayer();
    int count() const{return layers.count();}
    int layerNum() const{return layerNameCounter;}
    QImage currentCanvas();
    QImage allCanvas();
    int jitterCorrectionLevel() const;
    bool isJitterCorrectionEnabled() const;

    virtual QSize sizeHint () const;
    //virtual QSize minimumSizeHint () const;
    QSizePolicy sizePolicy() const;

    AntialiasingModes antialiasingMode() const;
    void setAntialiasingMode(AntialiasingModes mode);
    QPoint visualAreaPos() const; //return visual area's top left position in content coordinate
    QRect visualArea() const; //return actual area that can be used to show content(window rect - scrollbars)
    QRect visualContentArea() const; //return visual area's rect in content coordinate
    int scaleFactor() const; //return current scale factor
    QPoint mapToContent(const QPoint& posInVisual) const; //map point in visual area to content coordinate
    QPoint mapToVisualArea(const QPoint &posInContent) const; //map point in content to visual area coordinate

public slots:
    void setJitterCorrectionEnabled(bool correct);
    void setJitterCorrectionLevel(int value);
    void setShareColor(bool b);
    void setBrushColor(const QColor &newColor);
    void setBrushWidth(int newWidth);
    void setBrushHardness(int h);
    void setBrushThickness(int t);
    void setBrushWater(int w);
    void setBrushExtend(int e);
    void setBrushMixin(int e);
    void setBrushSettings(const QVariantMap& settings);
    void addLayer(const QString &name);
    bool deleteLayer(const QString &name);
    void clearLayer(const QString &name);
    void clearAllLayer();
    void lockLayer(const QString &name);
    void unlockLayer(const QString &name);
    void hideLayer(const QString &name);
    void showLayer(const QString &name);
    void moveLayerUp(const QString &name);
    void moveLayerDown(const QString &name);
    void layerSelected(const QString &name);
    void changeBrush(const QString &name);
    void onColorPicker(bool in);
    void onMoveTool(bool in);
    void loadLayers();
    void saveLayers();
    QList<QImage> layerImages() const;
    void pause();

    void moveVisualAreaTo(const QPoint &posInContent); //move visual area top left corner to a certain point in content
    void setScaleFactor(int factor); //scale with current visualAreaTopLeftPos;
    void setScaleFactor(int factor, const QPoint &originPosInVisual); //scale with originPos

signals:
    void contentMovedBy(const QPoint&);
    void canvasToolComplete();
    void newBrushSettings(const QVariantMap &map);
    void historyComplete();
    void newPaintAction(const QVariantMap m);
    void requestSortedMembers(CanvasBackend::MemberSectionIndex index
                               = CanvasBackend::MemberSectionIndex::Count);
    void requestClearMembers();
    void canvasExported(const QPixmap& pic);
    void parsePaused();
protected:
    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void paintEvent(QPaintEvent *event);
    void resizeEvent(QResizeEvent *event);
#ifndef PAINTTY_NO_TABLET
    void tabletEvent(QTabletEvent *ev);
#endif
    void focusInEvent(QFocusEvent * event);
    void focusOutEvent(QFocusEvent * event);

private slots:
    void remoteDrawPoint(const QPoint &point,
                         const QVariantMap &brushSettings,
                         const QString &layer,
                         const QString clientid,
                         const qreal pressure=1.0);
    void remoteDrawLine(const QPoint &start,
                        const QPoint &end,
                        const QVariantMap &brushSettings,
                        const QString &layer,
                        const QString clientid,
                        const qreal pressure=1.0);
    void onMembersSorted(const QList<CanvasBackend::MemberSection> &list);
private slots:
    void horizontalScroll(int value); //scroll the horizontal scrollbar
    void verticalScroll(int value); //scroll the vertical scrollbar


private:
    void drawLineTo(const QPoint &endPoint, qreal pressure=1.0);
    void drawPoint(const QPoint &point, qreal pressure=1.0);
    void storeAction(const QVariantMap& map);
    void sendAction();
    void pickColor(const QPoint &point);
    void updateCursor();
    void tryJitterCorrection();
    QImage appendAuthorSignature(QImage target);
    BrushPointer brushFactory(const QString &name);
    void setBrushFeature(const QString& key, const QVariant& value);
    void drawAuthorTips(QPainter &painter, const QPoint &pos, const QString &name);

    enum CONTROL_MODE {
        UNKNOWN = -1,
        NONE = 0,
        PICKING,
        DRAWING,
        MOVING
    };

    CONTROL_MODE control_mode_;
    QSize canvasSize;
    LayerManager layers;
    QImage image;
    QImage *currentImage;
    QPoint lastPoint;
    QList<QPoint> stackPoints;
    int layerNameCounter;
    BrushPointer brush_;
    bool shareColor_;
    bool jitterCorrection_;
    int jitterCorrectionLevel_;
    qreal jitterCorrectionLevel_internal_;
    QHash<QString, BrushPointer> remoteBrush;
    QHash<QString, BrushPointer> localBrush;
    CanvasBackend* backend_;
    QThread *worker_;
    QList<CanvasBackend::MemberSection> author_list_;
    QVariantList action_buffer_;

    QScrollBar *horizontalScrollBar;
    QScrollBar *verticalScrollBar;
    QImage content;
    QPoint visualAreaTopLeftPos;
    int m_scaleFactor;
    AntialiasingModes m_antialiasingMode;

    QPoint mouseDragStartPoint;

    void updateVisualArea(); //update visual area to show new area of content
    void adjustScrollBar(); //setup scrollbars' range, page step and value
};
Q_DECLARE_OPERATORS_FOR_FLAGS(Canvas::AntialiasingModes)

#endif // CANVAS_H
