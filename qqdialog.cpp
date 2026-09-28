#include "qqdialog.h"
#include "ui_QqDialog.h"
#include "softkeyboardbinder.h"
#include <QMenu>
#include <QDebug>
QqDialog::QqDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::QqDialog)
{
    ui->setupUi(this);
    ui->listEmojis->setVisible(false);
    ui->textMessageInput->setAttribute(Qt::WA_InputMethodEnabled,true);

    // 给文本框绑定软键盘（板子上没有物理键盘）
    SoftKeyboardBinder::attach(this, {
        ui->textMessageInput,     // QTextEdit 聊天输入框
        ui->lineEditNickname,     // QLineEdit 昵称
        ui->lineEditServerIp,     // QLineEdit 服务器 IP
        ui->lineEditSearch,       // QLineEdit 搜索
    });
}

QqDialog::~QqDialog()
{
    delete ui;
}

void QqDialog::on_btnBack_clicked()
{
    this->reject();
}

void QqDialog::on_btnLogin_clicked()
{
    ui->stackedWidget->setCurrentIndex(1);
}


void QqDialog::on_btnMenu_clicked()
{
    QMenu menu;
    menu.addAction("聊天记录管理");
    menu.addAction("检查更新");
    menu.addAction("帮助");
    menu.addAction("锁定");
    menu.addAction("设置");
    menu.addAction("退出账号");
    menu.setStyleSheet(R"(
QMenu {
    background-color: rgba(255,255,255,220); /*背景，220是透明度，255不透明*/
    border-radius: 8px;    /*圆角*/
    border: 1px solid #cccccc;
}
QMenu::item {
    padding:8px 24px;      /*上下、左右内边距，控制每行高度*/
    font-size:14px;
    color:#222222;
}
QMenu::item:selected {
    background-color:rgba(120,180,255,0.4); /*选中高亮颜色*/
}
)");

    // 按钮右下角，向上弹出
QPoint pos = ui->btnFiles->mapToGlobal(QPoint(ui->btnFiles->width(), 0));
    QAction *act=menu.exec(pos);
    if(act){
        if(act->text()=="退出账号"){
            ui->stackedWidget->setCurrentIndex(0);
        }
    }
}

void QqDialog::on_listFriends_itemClicked(QListWidgetItem *item)
{
    qDebug()<<"进入聊天"<<item->text();
    ui->stackedWidget->setCurrentIndex(2);
}


void QqDialog::on_btnSend_clicked()
{
    QString htmlMsg = ui->textMessageInput->toHtml();
    ui->textChatDisplay->setHtml(htmlMsg);
    ui->textMessageInput->clear();
}

void QqDialog::on_btnClose_clicked()
{
    ui->stackedWidget->setCurrentIndex(1);
}


void QqDialog::on_btnToolEmoji_clicked()
{
    bool show  =ui->listEmojis->isVisible();
    ui->listEmojis->setVisible(!show);
}


#include <QTextCursor>
#include <QTextImageFormat>
#include <QTextDocument>

void QqDialog::on_listEmojis_itemClicked(QListWidgetItem *item)
{
    QIcon icon = item->icon();
    QPixmap pix = icon.pixmap(32,32);
    QImage img = pix.toImage();

    QTextDocument *doc = ui->textMessageInput->document();
    static int id = 0;
    QString resName = QString("emoji_%1").arg(id++);
    doc->addResource(QTextDocument::ImageResource, QUrl(resName), QVariant(img));

    QTextCursor cursor = ui->textMessageInput->textCursor();
    cursor.insertImage(resName);
    ui->textMessageInput->setTextCursor(cursor);

    ui->listEmojis->hide();
}
