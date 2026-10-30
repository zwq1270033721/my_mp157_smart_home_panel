#ifndef GALAGAME_H
#define GALAGAME_H

#include <QWidget>

namespace Ui {
class galagame;
}

class galagame : public QWidget
{
    Q_OBJECT

public:
    explicit galagame(QWidget *parent = nullptr);
    ~galagame();

private:
    Ui::galagame *ui;
};

#endif // GALAGAME_H
