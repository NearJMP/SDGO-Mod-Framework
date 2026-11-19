# Client resource capacity

[简体中文](client-resource-capacity.zh-CN.md)

In Mod Manager, select a DIY client and use **客户端 LAA** after closing that client. Manager supports the registered 32-bit game image and checks its complete original digest before changing the PE flag. Running the same action again restores the original bytes. Managed clients follow their server owner's runtime package.

On 64-bit Windows, LAA lets the 32-bit game use up to 4 GiB of virtual address space. Map compilation and import run in the 64-bit Manager. Build a publication in **地图与任务导入**, or choose **从磁盘安装地图 ZIP** for an existing publication. Manager verifies and stages the archive from disk, then installs it while the selected client is closed. Large publications use up to 32 native archive parts and 2 GiB of archive data in total. Restart the client after installation and apply the matching server plan for new maps.

## Live resource worker

New map publications above 128 MiB of native archives declare `resource_worker` 1.0.0 and `map_catalog` 0.8.0 as requirements. Mod Manager 1.16.56 or later is required on the playing client. Mod Manager shows the requirement on compilation and checks it before installation. Keep the worker enabled in the client profile when distributing one of these packages.

| Component | Relationship |
| --- | --- |
| Map Catalogue | Requests the worker for marked map packages. Existing packages retain their original loading path. |
| 64-bit Mod Manager | Runs the session worker from the game directory. The worker validates, decompresses and caches individual resources during play. |
| Capsule, Unit and Operator catalogues | Share the native resource broker when active. Use matching framework programmes. Their own archives keep their existing loading paths, and installing these Mods is optional for maps. |
| Map server policy and LAA | Separate features. The map still needs its matching server plan. LAA can be configured independently of the worker. |

The worker starts when a marked map package is mounted. If it cannot start, the client leaves that package unregistered. If it disconnects during play, already mounted maps can read the exact sealed original entries for the rest of that session. The engine's per-resource, model and graphics limits still apply.
