#include "plugin.h"
#include <list>
#include "common.h"
#include "CWorld.h"
#include "CGeneral.h"
#include "CBaseModelInfo.h"
#include "CPedModelInfo.h"
#include "CModelInfo.h"
#include "CTxdStore.h"
#include "CCutsceneMgr.h"
#include "CTimer.h"
#include "CStreaming.h"
#include "CKeyGen.h"
#include <time.h>
#include "IniReader\IniReader.h"
#include "..\injector\assembly.hpp"

using namespace plugin;
using namespace std;
using namespace injector;

const int TEXTURE_LIMIT = 8;

fstream lg;
bool useLog = false;
bool txdsNotLoadedYet = true;
bool alreadyLoaded = false;
unsigned int cutsceneRunLastTime = 0;
uintptr_t ORIGINAL_AssignRemapTxd = 0;
uintptr_t ORIGINAL_RwTexDictionaryFindNamedTexture = 0;

int gangHandsTxdIndex = 0;
RwTexDictionary* handsDict;
RwTexture* handsBlack;
RwTexture* handsWhite;

void FindRemaps(CPed* ped);

class PedExtended
{
public:
	int curRemapNum[TEXTURE_LIMIT];
	int TotalRemapNum[TEXTURE_LIMIT];
	RwTexture * originalRemap[TEXTURE_LIMIT];
	std::list<RwTexture*> remaps[TEXTURE_LIMIT];

	PedExtended(CPed *ped)
	{
		for (int i = 0; i < TEXTURE_LIMIT; ++i)
		{
			originalRemap[i] = nullptr;
			curRemapNum[i] = -1;
			TotalRemapNum[i] = 0;
		}
	}
};

PedExtendedData<PedExtended> extData;

class DontRepeatIt
{
public:
	int modelId;
	int lastNum[TEXTURE_LIMIT];

	DontRepeatIt()
	{
		modelId = -1;
		for (int i = 0; i < TEXTURE_LIMIT; ++i)
		{
			lastNum[i] = -1;
		}
	}
};

RwTexDictionary* pedstxdArray[4];
int pedstxdIndexArray[4];
bool anyAdditionalPedsTxd;

void CustomAssignRemapTxd(const char* txdName, uint16_t txdId)
{
	if (txdName) {

		size_t len = strlen(txdName);

		if (len > 1) {
			//if (useLog) lg << "trying txd " << txdName << ".txd\n";
			if (isdigit(txdName[len - 1]))
			{
				if (strncmp(txdName, "peds", 3) == 0)
				{
					int arrayIndex = txdName[len - 1] - '0' - 1;
					if (arrayIndex < 4) {
						pedstxdIndexArray[arrayIndex] = txdId;
						CTxdStore::AddRef(pedstxdIndexArray[arrayIndex]);
						anyAdditionalPedsTxd = true;
						if (useLog) lg << "Found additional peds" << arrayIndex + 1 << ".txd\n";
					}
					else {
						if (useLog) lg << "ERROR: peds*.txd limit is only up to 'peds5.txd' \n";
						if (useLog) lg.flush();
					}
				}
				if (gangHandsTxdIndex == 0 && strncmp(txdName, "ganghands", 9) == 0)
				{
					gangHandsTxdIndex = txdId;
					CTxdStore::AddRef(txdId);
				}
			}
		}
	}
	plugin::CallDynGlobal< const char*, uint16_t>(ORIGINAL_AssignRemapTxd, txdName, txdId);
}

void LoadAdditionalTxds()
{
	txdsNotLoadedYet = false;
	bool anyRequest = false;

	if (gangHandsTxdIndex) {
		CStreaming::RequestTxdModel(gangHandsTxdIndex, (eStreamingFlags::GAME_REQUIRED | eStreamingFlags::KEEP_IN_MEMORY));
		handsDict = ((RwTexDictionary * (__cdecl*)(int)) 0x408340)(gangHandsTxdIndex); //size_t __cdecl getTexDictionary(int txdIndex)
		anyRequest = true;
	}

	if (anyAdditionalPedsTxd) {
		for (int i = 0; i < 4; ++i)
		{
			if (pedstxdIndexArray[i]) {
				CStreaming::RequestTxdModel(pedstxdIndexArray[i], (eStreamingFlags::GAME_REQUIRED | eStreamingFlags::KEEP_IN_MEMORY));
				//CStreaming::RequestTxdModel(pedstxdIndexArray[i], 8);
				if (useLog) lg << "Loading additional txd id " << (int)pedstxdIndexArray[i] << endl;
			}
		}

		CStreaming::LoadAllRequestedModels(false);
		anyRequest = false;

		for (int i = 0; i < 4; ++i)
		{
			if (pedstxdIndexArray[i]) {
				pedstxdArray[i] = ((RwTexDictionary * (__cdecl*)(int)) 0x408340)(pedstxdIndexArray[i]); //size_t __cdecl getTexDictionary(int txdIndex)
			}
		}
	}

	if (anyRequest) {
		CStreaming::LoadAllRequestedModels(false);
	}
}

RwTexture* __cdecl Custom_RwTexDictionaryFindNamedTexture(RwTexDictionary* dict, const char* name)
{
	RwTexture* texture;

	texture = plugin::CallAndReturnDynGlobal<RwTexture*, RwTexDictionary*, const char*>(ORIGINAL_RwTexDictionaryFindNamedTexture, dict, name);
	//texture = RwTexDictionaryFindNamedTexture(dict, name);
	if (texture) return texture;

	if (anyAdditionalPedsTxd)
	{
		for (int i = 0; i < 4; ++i)
		{
			if (pedstxdArray[i])
			{
				texture = RwTexDictionaryFindNamedTexture(pedstxdArray[i], name);
				if (texture) return texture;
			}
		}
	}
	//if (useLog) lg << name << " texture not found \n";
	if (txdsNotLoadedYet && anyAdditionalPedsTxd) LoadAdditionalTxds(); // looks not really safe, but tested a lot, if some problem by using more than 1 peds*.txd, do something here
	return nullptr;
}

const int totalOfDontRepeatIt = 30;
DontRepeatIt* dontRepeatIt[totalOfDontRepeatIt];
int curDontRepeatItIndex;

class PedFuncs
{
public:

	PedFuncs()
	{
		srand(time(NULL));

		CIniReader ini("PedFuncs.ini");

		if (ini.data.size() != 0)
		{
			useLog = (ini.ReadInteger("Settings", "UseLog", false) == true);
		}

		static std::list<std::pair<unsigned int *, unsigned int>> resetEntries;

		for (int i = 0; i < totalOfDontRepeatIt; ++i)
		{
			dontRepeatIt[i] = new DontRepeatIt;
		}
		curDontRepeatItIndex = 0;

		if (useLog)
		{
			lg.open("PedFuncs.log", std::fstream::out | std::fstream::trunc);
			lg << "v0.5.1" << endl;
			if (ini.data.size() == 0)
			{
				lg << "Warning: Can't read ini file." << endl;
			}
		}

		Events::initRwEvent += []
		{
			memset(pedstxdArray, 0, sizeof(pedstxdArray));
			memset(pedstxdIndexArray, 0, sizeof(pedstxdIndexArray));

			ORIGINAL_AssignRemapTxd = ReadMemory<uintptr_t>(0x5B62C2 + 1, true);
			ORIGINAL_AssignRemapTxd += (GetGlobalAddress(0x5B62C2) + 5);

			patch::RedirectCall(0x5B62C2, CustomAssignRemapTxd, true);

			ORIGINAL_RwTexDictionaryFindNamedTexture = ReadMemory<uintptr_t>(0x4C7533 + 1, true);
			ORIGINAL_RwTexDictionaryFindNamedTexture += (GetGlobalAddress(0x4C7533) + 5);

			patch::RedirectCall(0x4C7533, Custom_RwTexDictionaryFindNamedTexture, true);
			patch::RedirectCall(0x731733, Custom_RwTexDictionaryFindNamedTexture, true); // for map etc
		};

		Events::initGameEvent += []
		{
			if (!alreadyLoaded) {
				alreadyLoaded = true;

				if (txdsNotLoadedYet) LoadAdditionalTxds();

				if (handsDict)
				{
					handsBlack = RwTexDictionaryFindHashNamedTexture(handsDict, CKeyGen::GetUppercaseKey("hands_black"));
					handsWhite = RwTexDictionaryFindHashNamedTexture(handsDict, CKeyGen::GetUppercaseKey("hands_white"));

					if (handsBlack && handsWhite)
					{
						injector::MakeInline<0x59EF79, 0x59EF82>([](injector::reg_pack& regs)
						{
							CPed* ped = *(CPed**)(regs.esp + 0x1C + 0x8);
							CPedModelInfo* pedModelInfo = (CPedModelInfo*)CModelInfo::GetModelInfo(ped->m_nModelIndex);

							if (pedModelInfo->m_nPedType == ePedType::PED_TYPE_GANG1 || pedModelInfo->m_nPedType == ePedType::PED_TYPE_GANG2)
							{
								// normally black people
								regs.eax = (uint32_t)handsBlack;
							}
							else
							{
								//normally white people
								regs.eax = (uint32_t)handsWhite;
							}
						});
					}
				}

			}
		};

		Events::processScriptsEvent += []
		{
			if (CCutsceneMgr::ms_running) cutsceneRunLastTime = CTimer::m_snTimeInMilliseconds;
		};
		

		Events::pedSetModelEvent += [](CPed *ped, int model)
		{
			if ((CTimer::m_snTimeInMilliseconds - cutsceneRunLastTime) > 3000) FindRemaps(ped);
		};


		Events::pedRenderEvent.before += [](CPed *ped)
		{
			if (ped->m_pRwClump && ped->m_pRwClump->object.type == rpCLUMP)
			{
				PedExtended &info = extData.Get(ped);

				if (info.curRemapNum[0] >= 0)
				{
					RpClumpForAllAtomics(ped->m_pRwClump, [](RpAtomic *atomic, void *data)
					{
						PedExtended * info = reinterpret_cast<PedExtended*>(data);
						if (atomic->geometry)
						{
							RpGeometryForAllMaterials(atomic->geometry, [](RpMaterial *material, void *data)
							{
								PedExtended * info = reinterpret_cast<PedExtended*>(data);

								resetEntries.push_back(std::make_pair(reinterpret_cast<unsigned int *>(&material->texture), *reinterpret_cast<unsigned int *>(&material->texture)));

								for (int i = 0; i < TEXTURE_LIMIT; ++i)
								{
									if (info->curRemapNum[i] >= 0 && info->originalRemap[i] > 0)
									{
										if (info->originalRemap[i] == material->texture)
										{
											if (!info->remaps[i].empty())
											{
												list<RwTexture*>::iterator it = info->remaps[i].begin();
												advance(it, info->curRemapNum[i]);
												material->texture = *it;
											}
										}
									}
									else break;
								}
								return material;
							}, info);
						}
						return atomic;
					}, &info);
				}

			}
		};

		Events::pedRenderEvent.after += [](CPed *ped)
		{
			for (auto &p : resetEntries)
				*p.first = p.second;
			resetEntries.clear();
		};
	}


} pedFuncs;

int GetIndexFromTexture(PedExtended *info, string name, RwTexDictionary * pedTxdDic)
{
	int i;
	for (i = 0; i < TEXTURE_LIMIT; ++i)
	{
		if (info->originalRemap[i] > 0)
		{
			string nameStr = info->originalRemap[i]->name;
			if (nameStr.compare(name) == 0) return i;
		}
		else
		{
			info->originalRemap[i] = RwTexDictionaryFindNamedTexture(pedTxdDic, &name[0]);
			//lg << "added texture name to list: " << name << endl;
			return i;
		}
	}
	return -1;
}

void StoreSimpleRandom(PedExtended &info, int i)
{
	info.curRemapNum[i] = Random(-1, (info.TotalRemapNum[i] - 1));
	dontRepeatIt[curDontRepeatItIndex]->lastNum[i] = info.curRemapNum[i];
	//lg << info.curRemapNum[i] << " get in simple " << endl;
}

void LogRemaps(CPed *ped, PedExtended& info)
{
	if (useLog)
	{
		lg << "Model " << ped->m_nModelIndex << " total ";
		for (int i = 0; i < TEXTURE_LIMIT; ++i)
		{
			lg << i << ":" << info.TotalRemapNum[i] << " ";
		}
		lg << "selected ";
		for (int i = 0; i < TEXTURE_LIMIT; ++i)
		{
			lg << i << ":" << info.curRemapNum[i] << " ";
		}
		lg << endl;
	}
}

void FindRemaps(CPed * ped)
{
	PedExtended &info = extData.Get(ped);
	info = PedExtended(ped);

	CBaseModelInfo * pedModelInfo = (CBaseModelInfo *)CModelInfo::GetModelInfo(ped->m_nModelIndex);
	if (pedModelInfo)
	{
		TxdDef pedTxd = CTxdStore::ms_pTxdPool->m_pObjects[pedModelInfo->m_nTxdIndex];

		RwTexDictionary * pedTxdDic = pedTxd.m_pRwDictionary;
		if (pedTxdDic)
		{
			RwLinkList *objectList = &pedTxdDic->texturesInDict;
			if (!rwLinkListEmpty(objectList))
			{
				RwTexture * texture;
				RwLLLink * current = rwLinkListGetFirstLLLink(objectList);
				RwLLLink * end = rwLinkListGetTerminator(objectList);

				std::string originalRemapName;

				current = rwLinkListGetFirstLLLink(objectList);
				while (current != end)
				{
					texture = rwLLLinkGetData(current, RwTexture, lInDictionary);

					std::size_t found;
					string name = texture->name;
					found = name.find("_remap");
					if (found != std::string::npos)
					{
						int index = GetIndexFromTexture(&info, name.substr(0, found), pedTxdDic);
						//lg << "found remap: " << texture->name << " index " << index << endl;
						if (index != -1) // hit max
						{
							info.remaps[index].push_back(texture);
							info.TotalRemapNum[index]++;
						} else if (useLog) lg << "WARNING: CAN'T ADD REMAP FOR TEXTURE " << texture->name << " DUE TO " << TEXTURE_LIMIT << " TEXTURES LIMIT!" << endl;
					}

					current = rwLLLinkGetNext(current);
				}
				if (info.TotalRemapNum[0] > 0)
				{
					int lastNum = -1;

					for (int arrayIn = 0; arrayIn < totalOfDontRepeatIt; arrayIn++)
					{
						//lg << "array " << arrayIn << " model " << dontRepeatIt[arrayIn]->modelId << endl;
						if (dontRepeatIt[arrayIn]->modelId == ped->m_nModelIndex)
						{
							for (int i = 0; i < TEXTURE_LIMIT; ++i)
							{
								if (info.TotalRemapNum[i] > 1)
								{
									lastNum = dontRepeatIt[arrayIn]->lastNum[i];
									//lg << lastNum << " in model " << ped->m_nModelIndex << " array " << arrayIn << endl;
									do
									{
										info.curRemapNum[i] = Random(-1, (info.TotalRemapNum[i] - 1));
									} while (info.curRemapNum[i] == lastNum);
									//lg << info.curRemapNum[i] << " get in dont repeat - index " << i << endl;
									dontRepeatIt[arrayIn]->lastNum[i] = info.curRemapNum[i];
								}
								else
								{
									StoreSimpleRandom(info, i);
								}
							}
							LogRemaps(ped, info);
							return;
						}
					}
					//lg << "not found model " << ped->m_nModelIndex << " cur " << curDontRepeatItIndex << endl;
					dontRepeatIt[curDontRepeatItIndex]->modelId = ped->m_nModelIndex;
					curDontRepeatItIndex++;
					if (curDontRepeatItIndex >= totalOfDontRepeatIt) curDontRepeatItIndex = 0;
					for (int i = 0; i < TEXTURE_LIMIT; ++i)
					{
						if (info.TotalRemapNum[i] > 0)
						{
							StoreSimpleRandom(info, i);
						}
					}

					LogRemaps(ped, info);
					return;
				}
			}
		}
	}
}

extern "C" int32_t __declspec(dllexport) Ext_GetPedRemap(CPed * ped, int index)
{
	PedExtended &info = extData.Get(ped);
	if (useLog) lg << "Remaps: Get remap num: " << info.curRemapNum[index] << " index " << index << " for ped " << (int)ped << endl;
	return info.curRemapNum[index];
}

extern "C" void __declspec(dllexport) Ext_SetPedRemap(CPed * ped, int index, int num)
{
	PedExtended &info = extData.Get(ped);
	info.curRemapNum[index] = num;
	if (useLog) lg << "Remaps: New remap num: " << info.curRemapNum[index] << " index " << index << " for ped " << (int)ped << endl;
}

extern "C" void __declspec(dllexport) Ext_SetAllPedRemaps(CPed * ped, int num)
{
	for (int i = 0; i < TEXTURE_LIMIT; ++i)
	{
		PedExtended& info = extData.Get(ped);
		info.curRemapNum[i] = num;
	}
	if (useLog) lg << "Remaps: New all remaps num: " << num << " for ped " << (int)ped << endl;
}
