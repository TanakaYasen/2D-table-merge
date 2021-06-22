# 2D-table-merge
2D table merge library to solve 3-ways-merge problem

## Background & Purpose
一个游戏项目开发过程数据和代码由git管理。其中策划配置数据以一个2D-table形式表示。
Our project (a cyber game) is managed by git. Designer's configuration data is represented in a 2D-table format.

二维表非常普遍,如下例示定义了一个角色表：
2D-tables are very common, such as follows:

|	ID	|   name  | age | tall | vocation | address 
| :-- | :--: | :--: | :--: | :--: | :--: |
| 1	|	切糕茶叶蛋 |  28  | 171 | programmer | 上海市张扬北路3333号 |
| 2 | D.J.Trump | 72 | 196 | President | state FL street 233 |
| 3 | X | 33 | ? | ?? | ???
| 4 | ハンゾウ | 35 | 182 | sniper | 静岡県花村 |

现在游戏设计者在P 上分支A, B。 在A分支修改了 D.J.Trump的职业为Ex-President
另一个设计者在B分支修改D.J.Trump的年龄为74。然后两个版本合并了，冲突产生了，因为一行在两分支中同时修改而原生的合并工具不理解表格的行列语义。
我们希望合并后的表格是 `ID=2, name=D.J.Trump age=74, vocation = Ex-President`

**Merge Request** workflow allows parallel development. Now designer X checkout a branch as A, and modify Trump's vocation to Ex-President.
another designer Y checkout B, and modify Trump's age to 74. And finally two branches are merged, then a conflict comes out because both branches modify a **line** and native git cannot deal with it due to not compreham 2D-table symentic. We wanna the result to be `ID=2, name=D.J.Trump age=74, vocation = Ex-President`

Our purpose is to make that mergation affair easy.

## .tab format && how does it work

第一行. 列名
第二行. 注释 + 主键定义
第三行. 默认数据
4~$.	正式数据

借用关系数据库概念主键。主键是候选键的组合，唯一标识一行。

列主键很简单，单一列名就是主键。

行主键在第二行中用 *标识的候选键定义。所有标*的列内容按列序排，联成的键唯一标识一行。

### 原理
合并时，第一步，先确定最终有哪些列，以及每列的来源。比如不动的列，列名来自于原来三路的任意一列（选P路）， A路插入了一列COL_Y，则最终列含有COL_Y，且来源自A路。

第二步，再确定有哪些行，有些来自B。

第三步，如果一个cell 既有来源自A ,又有来源自B 则比较是否相同，相同不视为冲突，只有仅有一路，则信这一路的改变。


## How to Configurate

```
# project .gitattributes
*.tab merge=2Dtab-Merge
```

```
# .gitconfig
[merge "2Dtab-Merge"]
	name = 2Dtab
	driver = yourpath/2Dtab-Merge.exe cui_merge %O %A %B %L %P
```

.tab文件merge属性设定为 `2Dtab-Merge` 这个配置在.gitconfig中定义，主要是driver 指向2Dtab-Merge.exe

详情参见git doc

## Example

	base.ta

![Origin](https://github.com/TanakaYasen/2D-table-merge/blob/main/image/base.jpg?raw=true)

	ours.tab
	
![Ours](https://github.com/TanakaYasen/2D-table-merge/blob/main/image/ours.jpg?raw=true)
	
	theirs.tab
	
![Theirs](https://github.com/TanakaYasen/2D-table-merge/blob/main/image/theirs.jpg?raw=true)
	
	merged.tab
![Merged](https://github.com/TanakaYasen/2D-table-merge/blob/main/image/merged.jpg?raw=true)

## Build

本库由纯C(C99) 写成 这个库要小巧要快要低消耗。几乎C89也可以编译，除了//注释和柔性数组以外。平台相关性只有两个文件 solve.c  tab_log.c 需要用户手动改适配平台。

只公开后端合并算法。你需要写一个前端来调用这个库。比如弹对话框来提示等都是因平台而异的。

比如 2Dtab-Merge.exe 就使用cui_merge来调用`tab_merge_whole`接口。

this libaray is write in pure C(C99), to pursuit max speed because there are so frequent and large-quantium tab files changes in a 3-way-merge.

## TODO

add a makefile

memory leak check


