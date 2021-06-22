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

现在游戏设计者在P 上分支A, B。 在A分支修改了 D.J.Trump的职业 President => Ex-President
另一个设计者在B分支修改D.J.Trump的年龄为74。然后两个版本合并了，冲突产生了，因为一行在两分支中同时修改而原生的合并工具不理解表格的行列语义。
我们希望合并后的表格是 `ID=2, name=D.J.Trump age=24, vocation = Ex-President`

**Merge Request** workflow allows parallel development. Now designer X checkout a branch as A, and modify Trump's vocation.
another designer Y checkout B, and modify Trump's age. And finally two branches are merged, then a conflict comes out because both branches modify a **line** and native git cannot deal with it due to not compreham 2D-table symentic. We wanna the result to be `ID=2, name=D.J.Trump age=24, vocation = Ex-President`

Our purpose is to make that mergation affair easy.



