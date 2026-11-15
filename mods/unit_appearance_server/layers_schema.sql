-- Framework-owned cosmetic designs and exactly-once consumption receipts.
-- Native equipment, ownership, item definitions and accounts remain native.
SET NOCOUNT ON;
SET XACT_ABORT ON;
BEGIN TRANSACTION;
DECLARE @lock int;
EXEC @lock=sys.sp_getapplock @Resource=N'sdgo_mod_surface_layers_schema',
    @LockMode='Exclusive',@LockOwner='Transaction',@LockTimeout=5000;
IF @lock<0 BEGIN ROLLBACK; RAISERROR('Surface layer schema lock unavailable',16,1); RETURN; END;
IF OBJECT_ID(N'dbo.sdgo_mod_surface_layers',N'U') IS NULL
    CREATE TABLE dbo.sdgo_mod_surface_layers(
        UserUnitNumber bigint NOT NULL PRIMARY KEY,
        Revision bigint NOT NULL CHECK(Revision>0),
        Payload varbinary(1480) NOT NULL,
        UpdatedAt datetime2 NOT NULL DEFAULT SYSUTCDATETIME()
    );
IF OBJECT_ID(N'dbo.sdgo_mod_surface_layer_receipts',N'U') IS NULL
    CREATE TABLE dbo.sdgo_mod_surface_layer_receipts(
        Token binary(16) NOT NULL PRIMARY KEY,
        AccountNumber int NOT NULL,
        UserUnitNumber bigint NOT NULL,
        Reply varbinary(1898) NOT NULL,
        CreatedAt datetime2 NOT NULL DEFAULT SYSUTCDATETIME()
    );
IF OBJECT_ID(N'dbo.sdgo_mod_sticker_unlocks',N'U') IS NULL
    CREATE TABLE dbo.sdgo_mod_sticker_unlocks(
        AccountNumber int NOT NULL,
        StickerID int NOT NULL,
        CreatedAt datetime2 NOT NULL DEFAULT SYSUTCDATETIME(),
        CONSTRAINT PK_sdgo_mod_sticker_unlocks PRIMARY KEY(AccountNumber,StickerID)
    );
COMMIT;
