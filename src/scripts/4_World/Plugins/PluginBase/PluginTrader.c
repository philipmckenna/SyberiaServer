modded class PluginTrader
{
	ref PluginTrader_Config m_config;
	ref map<int, TraderPoint> m_traderPoints = new map<int, TraderPoint>;
	ref map<int, ref PluginTrader_TraderServer> m_traderCache = new map<int, ref PluginTrader_TraderServer>;
	ref map<int, ref PluginTrader_Data> m_traderData = new map<int, ref PluginTrader_Data>;
	
	override void OnInit()
	{
		SyberiaDatabaseInit.InitIfNot();
		
		GetSyberiaRPC().RegisterHandler(SyberiaRPC.SYBRPC_CLOSE_TRADER_MENU, this, "RpcRequstTraderMenuClose"); 
		GetSyberiaRPC().RegisterHandler(SyberiaRPC.SYBRPC_ACTION_TRADER, this, "RpcRequstTraderAction"); 
		
		MakeDirectory("$profile:Syberia");

		string path = "$profile:Syberia\\TradingConfig.json";
		if (FileExist(path))
		{
			m_config = new PluginTrader_Config;
			JsonFileLoader<ref PluginTrader_Config>.JsonLoadFile(path, m_config);
		}
		else
		{
			Error("FAILED TO FIND " + path);
		}
	}
	
	void InitializeTraders()
	{
		if (m_config && m_config.m_traders)
		{
			foreach (ref PluginTrader_TraderServer trader : m_config.m_traders)
			{
				SpawnTrader(trader);
			}
		}
		else
		{
			Error("FAILED TO PARSE TRADER CONFIG");
		}
	}

	private void SpawnTrader(ref PluginTrader_TraderServer trader)
	{
		if (trader.m_traderId < 0)
		{
			Error("FAILED TO INITIALIZE TRADER WITH ID " + trader.m_traderId);
			return;
		}
				
		DatabaseResponse response = null;
		string selectQuery = "SELECT classname, data FROM trader_data WHERE trader_id = " + trader.m_traderId.ToString() + ";";
		GetDatabase().QuerySync(SYBERIA_DB_NAME, selectQuery, response);
		ref PluginTrader_Data traderData = new PluginTrader_Data;
		traderData.DeserializeFromDb( response );		
		m_traderData.Insert( trader.m_traderId, traderData );
		
		Object traderObj = GetGame().CreateObject(trader.m_classname, trader.m_position);
		if (!traderObj)
		{
			Error("FAILED TO INITIALIZE TRADER WITH ID " + trader.m_traderId);
			return;
		}
	
		traderObj.SetAllowDamage(false);
		traderObj.SetPosition(trader.m_position);
		traderObj.SetOrientation(Vector(trader.m_rotation, 0, 0));
				
		EntityAI traderEntity;
		if (EntityAI.CastTo(traderEntity, traderObj) && trader.m_attachments)
		{
			foreach (string attachment : trader.m_attachments)
			{
				traderEntity.GetInventory().CreateInInventory(attachment);
			}
		}
		
		PlayerBase traderHuman;
		if (PlayerBase.CastTo(traderHuman, traderObj))
		{
			traderHuman.MarkAsNPC();
		}
		
		TraderPoint traderPoint = TraderPoint.Cast( GetGame().CreateObject("TraderPoint", trader.m_position) );
		traderPoint.SetPosition(trader.m_position);
		traderPoint.SetAllowDamage(false);
		traderPoint.InitTraderPoint(trader.m_traderId, traderObj);
				
		m_traderPoints.Insert(trader.m_traderId, traderPoint);		
		m_traderCache.Insert(trader.m_traderId, trader);
		
		SybLogSrv("Trader " + trader.m_traderId + " successfully initialized.");
	}

	void OnUpdateTraderDataDB(DatabaseResponse response, ref Param args)
	{
		// Do nothing
	}
	
	void SendTraderMenuOpen(PlayerBase player, int traderId)
	{
		if (!player)
			return;
		
		ref PluginTrader_TraderServer trader;
		if (!m_traderCache.Find(traderId, trader))
			return;
		
		TraderPoint traderPoint;
		if ( !m_traderPoints.Find(traderId, traderPoint) )
			return;
		
		ref PluginTrader_Data traderData;
		if ( !m_traderData.Find(traderId, traderData) )
			return;
		
		if (traderPoint.HasActiveUser())
		{
			GetSyberiaRPC().SendToClient(SyberiaRPC.SYBRPC_SCREEN_MESSAGE, player.GetIdentity(), new Param1<string>("#syb_trader_blocked"));
			return;
		}
		
		traderPoint.SetActiveUser(player);
		
		Param3<int, ref PluginTrader_Trader, ref PluginTrader_Data> params = 
			new Param3<int, ref PluginTrader_Trader, ref PluginTrader_Data>(traderId, trader, traderData);
		
		GetSyberiaRPC().SendToClient(SyberiaRPC.SYBRPC_OPEN_TRADE_MENU, player.GetIdentity(), params);
	}
	
	void RpcRequstTraderMenuClose(ref ParamsReadContext ctx, ref PlayerIdentity sender)
    {   
		Param1<int> clientData;
       	if ( !ctx.Read( clientData ) ) return;		
		
		TraderPoint traderPoint;
		if ( m_traderPoints.Find(clientData.param1, traderPoint) )
		{
			traderPoint.SetActiveUser(null);
		}
	}
	
	void RpcRequstTraderAction(ref ParamsReadContext ctx, ref PlayerIdentity sender)
	{
		// Prepare
		Param2<int, ref array<ItemBase>> clientData;
       	if ( !ctx.Read( clientData ) ) return;
		
		int traderId = clientData.param1;
		ref array<ItemBase>	sellItems = clientData.param2;
		
		ref PluginTrader_TraderServer traderInfo;
		if (!m_traderCache.Find(traderId, traderInfo))
			return;
		
		ref PluginTrader_Data traderData;
		if ( !m_traderData.Find(traderId, traderData) )
			return;
		
		// Validation
		int resultPrice = 0;
		foreach (ItemBase sellItem1 : sellItems)
		{
			resultPrice = resultPrice + CalculateSellPrice(traderInfo, traderData, sellItem1);
		}
		
		if (resultPrice < 0)
		{
			return;
		}
		
		// Sell items		
		TStringArray updateDbQueries = new TStringArray;
		foreach (ItemBase sellItem2 : sellItems)
		{
			if (sellItem2)
			{
				string classname = sellItem2.GetType();
				float maxQuantity = CalculateTraiderItemQuantityMax(traderInfo, classname);
				float itemQuantity = CalculateItemQuantity01(sellItem2);
				float newValue = 0;
				
				if (traderData.m_items.Contains(classname))
				{
					newValue = Math.Min(maxQuantity, traderData.m_items.Get(classname) + itemQuantity);
					traderData.m_items.Set( classname, newValue );
					updateDbQueries.Insert("UPDATE trader_data SET data = '" + newValue + "' WHERE trader_id = " + traderId + " AND classname = '" + classname + "';");
				}
				else
				{
					newValue = Math.Min(maxQuantity, itemQuantity); 
					traderData.m_items.Set( classname, newValue );
					updateDbQueries.Insert("INSERT INTO trader_data (trader_id, classname, data) VALUES (" + traderId + ", '" + classname + "', '" + newValue + "');");
				}
				
				SybLogSrv("TRADER " + traderId + " BUY ITEM " + classname + ":" + itemQuantity);
			}
		}
		
		// Update database
		GetDatabase().TransactionAsync(SYBERIA_DB_NAME, updateDbQueries, this, "OnUpdateTraderDataDB", null);
		
		// Delete sell items
		foreach (ItemBase sellItem3 : sellItems)
		{
			if (sellItem3)
			{
				GetGame().ObjectDelete(sellItem3);				
			}
		}
		
		// Send response
		GetSyberiaRPC().SendToClient(SyberiaRPC.SYBRPC_ACTION_TRADER, sender, new Param1<ref PluginTrader_Data>(traderData));
	}
	
	override void OnDestroy()
	{		
		if (m_config)
		{
			delete m_config;
		}
		
		if (m_traderPoints)
		{
			foreach (int id, TraderPoint obj : m_traderPoints)
			{
				GetGame().ObjectDelete(obj);
			}
			delete m_traderPoints;
		}
		
		delete m_traderCache;
		delete m_traderData;
	}
	
	static void InitQueries(ref array<string> queries)
	{
		queries.Insert("CREATE TABLE IF NOT EXISTS trader_data (id INTEGER PRIMARY KEY AUTOINCREMENT, trader_id INTEGER, classname TEXT NOT NULL, data TEXT NOT NULL);");
	}
};

class PluginTrader_Config
{
	ref array<ref PluginTrader_TraderServer> m_traders;
	
	void ~PluginTrader_Config()
	{		
		if (m_traders)
		{
			foreach (ref PluginTrader_TraderServer trader : m_traders)
			{
				delete trader;
			}
			delete m_traders;
		}
	}
};

class PluginTrader_TraderServer : PluginTrader_Trader
{
    string m_classname;
	ref array<string> m_attachments;
    vector m_position;
    float m_rotation;
};

modded class PluginTrader_Data
{
	void DeserializeFromDb(ref DatabaseResponse response)
	{
		m_items = new map<string, float>;
		
		if (!response)
			return;
		
		for (int i = 0; i < response.GetRowsCount(); i++)
		{
			m_items.Insert( response.GetValue(i, 0), response.GetValue(i, 1).ToFloat() );
		}
	}
};