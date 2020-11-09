# Envoy 中文文档翻译指导手册

## 概要

Istio 一直是服务网格产品中的佼佼者，其数据面的组件—— Envoy 也受到很多互联网厂家及 IT 行业人员的追捧和青睐。云原生社区秉承普及和推广云原生相关技术的宗旨，已经在早些时候成立了[Envoy SIG](https://cloudnative.to/blog/sig-envoy-announcement/)。为了方便国内 IT 行业人员学习与研究 Envoy，云原生社区决定将[Envoy 官网](https://www.envoyproxy.io/docs/envoy/)最新版本进行翻译（[Envoy 有1.7中文版本](https://www.servicemesher.com/envoy/)，但是版本过老）。

## 翻译负责人

| 责任范围 | 人员 |GitHub 账号 |
| :---: | :---: |:---: |
| 翻译跟踪、文档更新 | 马景贺 | [lhb008](https://github.com/lhb008)|
| 翻译跟踪、文档更新 | 许振文 | [helight](https://github.com/helight)|
| 翻译跟踪、文档更新 | 张晓辉 | [addozhang](https://github.com/addozhang)|

## 翻译计划

计划招募**30**名左右的翻译志愿者，组成 `Envoy` 官网翻译小组。翻译过程**采取迭代式管理**，一个周为一个迭代，每个迭代从社区例会（一般在周日晚上）开始，到下一次社区例会结束。每个迭代开始前会开放此迭代能够被认领的翻译任务，每个迭代结束时会在翻译群里面更新此迭代任务的翻译情况。这种方式能够保证翻译是按照章节顺序推进，而不是任由大家随意选取，造成翻译不可控的局面。

## 流程概述

大体的翻译流程如下图所示：

![](https://github.com/lhb008/envoy/blob/zh/docs/root/img/envoy-trans.png)

整个翻译的基本流程包括下面几个步骤：

- 成为翻译小组成员： [加入 Envoy SIG 并申请成为翻译小组成员](https://cloudnative.to/blog/envoy-trans-recruit/)
- 任务领取：在本仓库的 Issue 页面领取待翻译的任务；
- 翻译：根据任务提示进行翻译工作；
- 提交：翻译人员提交 PR 等待 review；
- 校对：其他翻译人员对当前任务进行 review；
- 终审：管理员团队对翻译内容进行最后确认；
- 预览：和源文档进行比对，查看显示效果；
- 合并：merge 至中文官方仓库，任务结束。

我们通过校对、终审两轮 review 保证翻译的质量；通过预览保证显示的准确性。翻译人员在整个流程中需要做的是领取任务，翻译，提交 PR，预览自查这几步。

## 约定

> 在开始翻译之前，请仔细阅读以下约定，了解项目的协作流程、避免一些常见的错误。

### 1 准备工作

- 账号：微信账号和 GitHub 账号。微信用于沟通协作，GitHub 用于认领任务及提交翻译。

- 登记个人信息：登记[志愿者个人信息](https://docs.qq.com/sheet/DYVJpRFdKbUVnQUZV?tab=BB08J2)。

### 2 术语表

为保证翻译的统一性和准确性，请在翻译前、翻译时、校对时参考[术语表](https://github.com/cloudnativeto/kubebuilder/blob/zh/docs/book/term.md)。

术语表内容包含以下几类：

- 常用词汇：对常见的技术词汇给出的推荐翻译，也可结合语境自行修改；
- 术语：文档中出现的专有技术名词、关键词、**需保持原文不翻译**；

[术语表](https://github.com/cloudnativeto/kubebuilder/blob/zh/docs/book/term.md)欢迎大家提 PR 共同更新维护。

### 3 合并分支

注意，我们使用 [cloudnativeto/envoy](https://github.com/cloudnativeto/envoy) 的 **zh** 分支来存放中文翻译文档。您的 PR 始终应该是合并至该分支。

### 4 工作分支

注意，我们建议为每个任务创建一个 Git 分支，以避免出现冲突。

您可以这样从 [cloudnativeto/envoy](https://github.com/cloudnativeto/envoy) 为您的任务 checkout 一个新分支。

```bash
$ cd <您 fork 的项目路径>
# git remote 命令只需要在您 clone 项目之后执行一次，后续不需要重复执行
$ git remote add upstream https://github.com/cloudnativeto/envoy.git
$ git fetch upstream zh:<自定义分支名>
$ git checkout <自定义分支名>
```

### 5 Label 和常用指令

[cloudnativeto/envoy](https://github.com/cloudnativeto/envoy) 项目使用 issue 进行任务管理，可以在[这里](https://github.com/cloudnativeto/envoy/issues)看到所有的 issue。

您可以通过 label 来判断 issue 当前的状态、执行不同的指令。常见的 label 及指令如下：

label   |  描述  | 说明 |
--------|--------|-----|
kind/page | 页面  | 拥有该 label 的 issue 对应着一个任务         |
status/pending | 待领取的任务  | 拥有该 label 的 issue 是一个`可以被领取`的任务，加入组织后，您可以通过 `/accept` 指令领取该任务。    |
status/waiting-for-pr | 待提交的任务 | 输入 `/accept` 指令后，任务会进入该状态。在您完成翻译并创建 PR 后，您可以通过 `/pushed` 指令更新任务的状态。 |
status/reviewing | 校对中的任务 | 输入 `/pushed` 指令后，任务会进入该状态。此时您需要关注校对人员给您的 PR 提出的修改建议。在 PR 被合并后，您可以输入 `/merged` 指令，以完成该任务。 |
status/finished | 已完成的任务 | 输入 `/merged` 指令后，任务会进入该状态。此时任务已经完成，issue 会自动关闭，您可以继续领取其它任务。|

## 翻译指南

### 1 领取任务

访问[这里](https://github.com/cloudnativeto/envoy/issues)查看任务列表，您可以根据 issue 的 label 来区分任务的状态。

![](https://cdn.sguan.top/markdown/20200724172537.png)

带有 `status/pending` label 的任务是可以领取的，在 issue 内评论 `/accept` 即可，随后机器人会自动将任务分配给您。 例如：

![](https://cdn.sguan.top/markdown/20200724172708.png)

### 2 添加 upstream 并 fetch 分支

如果您是`首次领取任务`，您需要先 fork 仓库，并创建新分支：

```bash
$ cd <您 fork 的项目路径>
$ git remote add upstream https://github.com/cloudnativeto/envoy.git
$ git fetch upstream zh:<自定义分支名>
$ git checkout <自定义分支名>
```

如果您`不是首次领取任务`，则不需要再添加 upstream，直接创建新分支即可：

```bash
$ cd <您 fork 的项目路径>
$ git fetch upstream zh:<自定义分支名>
$ git checkout <自定义分支名>
```

### 3 翻译

翻译任务相关内容，在此过程中，可以参考并共同完善[术语表](https://github.com/cloudnativeto/envoy/tree/zh/docs/root/term.md)。

### 4 本地构建和预览

> 由于 Envoy 文档的特殊性，本次翻译没有设置 netlify 做预览，只配置了 github action 做定时的翻译发布（每天两次）。当提交PR 之后，等待 reviewer review 之后将代码合入之后，才会在定时发布之后看到[翻译效果](https://cloudnative.to/envoy/)


### 5 提交 PR

向 [cloudnativeto/envoy](https://github.com/cloudnativeto/envoy.git) 的 `zh 分支` 提交 PR，并根据 `PR 模板`填写相关信息，在提交 PR 以后，您可以关注该页面 `netlify` 的构建结果，并进行预览。

```text
标题：
zh-translation: <file_full_path>

内容：
ref: https://github.com/cloudnativeto/envoy/issues/<issueID>

- [x] Docs
```

其中，标题中的 `<file_full_path>` 是翻译的源文件路径；内容中的 `ref` 是当前翻译任务的 issue 链接。

### 6 校对（Review）

#### 两轮 Review

为保证质量，我们设置了两轮 review：

- 初审：负责对翻译的内容和原文较为精细的进行对比，保证语句通顺，无明显翻译错误；

- 终审：负责对翻译的文档做概要性的检查，聚焦在行文的通顺性、一致性、符合中文语言习惯，词汇、术语准确。终审通过后由管理员 approve 当前 PR，就可以进行合并了。

#### PR 创建者

在创建 PR 后，需要由没有翻译该文档的`其他志愿者`对内容进行校对。这些人包括：热心志愿者主动帮忙校对、联系有时间帮助您进行校对的志愿者、由 maintainer 分配。

在校对人员给出修改意见后，应该及时的对相关的内容进行修改，有疑问的地方也可以一起讨论。

#### 校对人员

除 PR 创建者外，任何人都可以参与该 PR 的校对工作。

当有新的 PR 创建时，您可以在`钉钉群`收到相关消息，您也可以直接查看 [cloudnativeto/envoy#pulls](https://github.com/cloudnativeto/envoy/pulls) 找到您感兴趣的 PR，并回复 `/review`，随后 maintainer 会将该 PR 的校对任务分配给您。

##### 校对提要

- 打开 PR 提交的中文翻译，打开对应 issue 中指定的源文件，逐段进行走查；

- 词汇检查：检查译文中出现的术语、常用词汇是否遵照了[术语表](https://github.com/cloudnativeto/envoy/tree/zh/docs/root/term.md)的要求进行翻译；

- 格式检查：中文字符和英文字符用一个空格分隔、中文字符和数字用一个空格分隔、英文复数变单数。

- 格式检查：对照原文，检查译文中的标题和层次是否对应；代码块是否指定了语言；标点符号是否正确且无英文标点；超链接、图片链接是否可达；是否有错别字；

- 语句检查：分段落通读一遍，检查是否有不通顺、语病、或者不符合中文习惯的译文（啰嗦、重复、过多的助词等）

##### 修改意见

校对过程中发现的问题，在 PR 提交文件的对应行添加 comment，不确定的地方可与 PR 提交者一起讨论，或发到协作群进行讨论。

另外，我们建议使用 suggestion 对 PR 添加 comment，GitHub 为其提供了高亮对比，在 comment 时点击下图中红圈的按钮即可：

![](https://cdn.sguan.top/markdown/20200725103949.png)

这样 review comment 看起来会较为直观：

![](https://cdn.sguan.top/markdown/20200725104045.png)

完成 review 后，您可以 `@PR 创建者`，提醒他及时跟进并修改相关内容。

在 PR 完成修改、您觉得该 PR 可以合并后，您可以评论 `/LGTM @一位 maintainer`，随后 maintainer 会对 PR 进行终审。一切顺利的话，该 PR 即将会被合并！

### 7 完成任务

通过终审后的 PR 会被管理员 approve、并合并到 envoy 的中文官方仓库中。

在 PR 合并后，您需要在对应的任务 [issue](https://github.com/cloudnativeto/envoy/issues) 中输入指令 `/merged`，Bot 会设置 Issue 的状态为 `status/finished`，并关闭 issue。

至此，整个翻译任务就算正式完成了，您可以继续领取新的任务进行翻译，或参与校对工作。

## 翻译注意事项

* 中文语句中如果有 `English` 单词，则英文单词前后应该有空格。
* 文中遇到的超链接，不需要跳转超链接进行翻译（如果跳转至前面内容，则已经翻译结束，如果跳转到后面内容，则将来翻译，如果跳转到其他网站，则不予翻译）。
* 翻译是迭代式进行的，一周为一个迭代，请尽量在一个迭代内完成自己的翻译任务。
* 如您的 PR 已经被merge，请关闭相应的issue。
