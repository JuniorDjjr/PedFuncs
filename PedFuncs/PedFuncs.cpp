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
#include <time.h>
#include "IniReader\IniReader.h"

using namespace plugin;
using namespace std;

const int TEXTURE_LIMIT = 8;

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

fstream lg;
bool useLog = false;
const int totalOfDontRepeatIt = 30;
unsigned int cutsceneRunLastTime = 0;

DontRepeatIt *dontRepeatIt[totalOfDontRepeatIt];
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
			lg << "v0.4" << endl;
			if (ini.data.size() == 0)
			{
				lg << "Warning: Can't read ini file." << endl;
			}
		}


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
