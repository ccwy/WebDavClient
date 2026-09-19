webdav客户端支持Win 7  -  Win 11  支持自定义webdav端口，支持域名和IP链接，支持SSL。


程序已内置打包了运行环境及winfsp、rclone，故程序本体比较大，程序会根据需要自动释放对应资源包进行安装。


支持挂载成功后自动隐藏窗口，隐藏后可按Ctrl+shift+M恢复窗口和托盘


如果需要运行多个程序，请将程序放在不同的目录下，不同目录下可以实现多个客户端运行


webdav客户端下载地址：https://github.com/ccwy/WebDavClient/releases


win7系统：winfsp建议使用v1.1版本，rclone下载v1.63.1版本或者在本项目中下载，
路径 ：\WebDavClient\res\bin

win10及以上系统：winfsp和rclone均可使用最新版本，

winfsp需要安装到系统内，rclone需要放置在程序同目录下，

C语言编写，利用rclone和winfsp实现本地磁盘挂载

如您想自行下载资源问题，或者更新新版资源文件，可以在下方地址下载最新版的，本程序以内置了资源文件，但您可以通过替换的方式更新资源文件，只需要保持文件名不变就可以了


winfsp 下载地址：https://github.com/winfsp/winfsp/releases

rclone 下载地址：https://github.com/rclone/rclone/releases


<img width="566" height="473" alt="image" src="https://github.com/user-attachments/assets/a71d6110-4ccb-4062-9bf0-bd0e876496da" />

### v1.3版本更新

    1，增加系统版本精确检测，避免用户使用错误系统版本
    2，增加挂载成功自动隐藏窗口和托盘，包括开机自启
    3，优化错误提示
    4，去除VC++依赖，改用静态链接，减小安装包体积
    5，win7去除TLS补丁安装

### v1.4版本更新
    1，增加高级设置按钮
    2，增加rclone启动参数配置
    3，在高级设置里增加清理缓存功能
    4，高级设置里的，如果不懂，请保持默认即可
    5，缓存模式默认调整为full
    6，更多功能有待您的发现
