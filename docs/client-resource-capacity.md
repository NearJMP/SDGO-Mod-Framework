# Client resource capacity

[简体中文](client-resource-capacity.zh-CN.md)

In Mod Manager, select a DIY client and use **客户端 LAA** after closing that client. Manager supports the registered 32-bit game image and checks its complete original digest before changing the PE flag. Running the same action again restores the original bytes. Managed clients follow their server owner's runtime package.

On 64-bit Windows, LAA lets the 32-bit game use up to 4 GiB of virtual address space. Map compilation and import run in the 64-bit Manager. Build a publication in **地图与任务导入**, or choose **从磁盘安装地图 ZIP** for an existing publication. Manager verifies and stages the archive from disk, then installs it while the selected client is closed. Large publications use up to 32 native archive parts and 2 GiB of archive data in total. Restart the client after installation and apply the matching server plan for new maps.

The game opens individual resources from those files during play. The game's per-resource, model and graphics limits still apply.
