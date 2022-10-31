# libqb

**注意**：中文文档有可能未及时更新，请以最新的英文[README](./README.markdown)为准。

## libqb是什么

libqb 是为客户端-服务器架构提供高性能、可重用的功能的库，例如日志记录、跟踪、进程间通信 (IPC) 和轮询。

libqb 并非旨在成为一个包罗万象的库，而是专注于提供高度优化后的客户端-服务器应用程序API，来使客户端-服务器应用程序达到最佳性能。

## 可以通过如下方式了解更多信息

- [libqb wiki](https://github.com/clusterlabs/libqb/wiki)

- [Issues/Bugs](https://github.com/clusterlabs/libqb/issues)

- [doxygen生成的手册](http://clusterlabs.github.io/libqb/CURRENT/doxygen/)

- 您也可以使用如下命令自己构建手册

    ```shell
    $ make doxygen
    $ firefox ./doc/html/index.html
    ```

## 依赖

- glib-2.0-devel（如果您想构建glib样例代码）

- check-devel （如果您想运行测试样例）

- doxygen and graphviz （如果您想构建doxygen man手册或者html手册）

## 代码管理（git）

```shell
git clone git://github.com/ClusterLabs/libqb.git
```

[查看github](https://github.com/clusterlabs/libqb)

## 源码编译安装

```shell
$ ./autogen.sh
$ ./configure
$ make
$ sudo make install
```

## 您如何帮助我们

如果您觉得这个项目有用，您可以考虑支持它的未来发展。 有多种方法可以对该项目提供支持：

- 测试并提交问题

- 帮助 [developer@clusterlabs.org 邮件列表](http://clusterlabs.org/mailman/listinfo/developers)中的其他人

- 提交文档、示例和测试用例

- 提交补丁

- 推广项目
