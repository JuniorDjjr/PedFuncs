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

#include "PedFuncs.h"

using namespace plugin;
using namespace std;

class PedExtended {
public:
	int curRemapNum[4];
	int TotalRemapNum[4];
	RwTexture * originalRemap[4];
	std::list<RwTexture*> remaps[4];

	PedExtended(CPed *ped) {
		originalRemap[0] = nullptr;
		originalRemap[1] = nullptr;
		originalRemap[2] = nullptr;
		originalRemap[3] = nullptr;

		curRemapNum[0] = -1;
		curRemapNum[1] = -1;
		curRemapNum[2] = -1;
		curRemapNum[3] = -1;

		TotalRemapNum[0] = 0;
		TotalRemapNum[1] = 0;
		TotalRemapNum[2] = 0;
		TotalRemapNum[3] = 0;
	}
};

PedExtendedData<PedExtended> extData;

class DontRepeatIt {
public:
	int modelId;
	int lastNum[4];

	DontRepeatIt() {
		modelId = -1;
		lastNum[0] = -1;
		lastNum[1] = -1;
		lastNum[2] = -1;
		lastNum[3] = -1;
	}
};


fstream fs;
const int totalOfDontRepeatIt = 30;
unsigned int cutsceneRunLastTime = 0;

DontRepeatIt *dontRepeatIt[totalOfDontRepeatIt];
int curDontRepeatItIndex;

class PedFuncs {
public:

	PedFuncs() {
		srand(time(NULL));

		static std::list<std::pair<unsigned int *, unsigned int>> resetEntries;

		for (int i = 0; i < totalOfDontRepeatIt; i++) {
			dontRepeatIt[i] = new DontRepeatIt;
		}
		curDontRepeatItIndex = 0;

		fs.open("PedFuncs.log", std::fstream::out | std::fstream::trunc);

		fs << "v0.3.1\n";


		Events::processScriptsEvent += [] {
			if (CCutsceneMgr::ms_running) cutsceneRunLastTime = CTimer::m_snTimeInMilliseconds;
		};
		

		Events::pedSetModelEvent += [](CPed *ped, int model) {
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

								for (int i = 0; i < 4; i++)
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

		// Flush log
		Events::onPauseAllSounds += []
		{
			fs.flush();
		};
	}


} pedFuncs;

int GetIndexFromTexture(PedExtended *info, string name, RwTexDictionary * pedTxdDic)
{
	int i;
	for (i = 0; i < 4; i++)
	{
		if (info->originalRemap[i] > 0)
		{
			string nameStr = info->originalRemap[i]->name;
			if (nameStr.compare(name) == 0) return i;
		}
		else
		{
			info->originalRemap[i] = RwTexDictionaryFindNamedTexture(pedTxdDic, &name[0]);
			//fs << "added texture name to list: " << name << "\n";
			return i;
		}
	}
	return -1;
}

void StoreSimpleRandom(PedExtended &info, int i)
{
	info.curRemapNum[i] = Random(-1, (info.TotalRemapNum[i] - 1));
	dontRepeatIt[curDontRepeatItIndex]->lastNum[i] = info.curRemapNum[i];
	//fs << info.curRemapNum[i] << " get in simple " << " \n";
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
						//fs << "found remap: " << texture->name << " index " << index << "\n";
						if (index != -1) // hit max
						{
							info.remaps[index].push_back(texture);
							info.TotalRemapNum[index]++;
						} else fs << "WARNING: CAN'T ADD REMAP FOR TEXTURE " << texture->name << " DUE TO 4 TEXTURES LIMIT!\n";
					}

					current = rwLLLinkGetNext(current);
				}
				if (info.TotalRemapNum[0] > 0)
				{
					fs << "Model " << ped->m_nModelIndex << " remaps 0:" << info.TotalRemapNum[0] << " 1:" << info.TotalRemapNum[1] << " 2:" << info.TotalRemapNum[2] << " 3:" << info.TotalRemapNum[3] << "\n";
					int lastNum = -1;

					for (int arrayIn = 0; arrayIn < totalOfDontRepeatIt; arrayIn++)
					{
						//fs << "array " << arrayIn << " model " << dontRepeatIt[arrayIn]->modelId << " \n";
						if (dontRepeatIt[arrayIn]->modelId == ped->m_nModelIndex)
						{
							for (int i = 0; i < 4; i++)
							{
								if (info.TotalRemapNum[i] > 1)
								{
									lastNum = dontRepeatIt[arrayIn]->lastNum[i];
									//fs << lastNum << " in model " << ped->m_nModelIndex << " array " << arrayIn << " \n";
									do {
										info.curRemapNum[i] = Random(-1, (info.TotalRemapNum[i] - 1));
									} while (info.curRemapNum[i] == lastNum);
									//fs << info.curRemapNum[i] << " get in dont repeat - index " << i << " \n";
									dontRepeatIt[arrayIn]->lastNum[i] = info.curRemapNum[i];
								}
								else
								{
									StoreSimpleRandom(info, i);
								}
							}
							return;
						}
					}
					//fs << "not found model " << ped->m_nModelIndex << " cur " << curDontRepeatItIndex << " \n";
					dontRepeatIt[curDontRepeatItIndex]->modelId = ped->m_nModelIndex;
					curDontRepeatItIndex++;
					if (curDontRepeatItIndex >= totalOfDontRepeatIt) curDontRepeatItIndex = 0;
					for (int i = 0; i < 4; i++)
					{
						if (info.TotalRemapNum[i] > 0)
						{
							StoreSimpleRandom(info, i);
						}
					}
					return;
				}
			}
		}
	}
}

extern "C" void __declspec(dllexport) ignore() { return; };

extern "C" int32_t __declspec(dllexport) Ext_GetPedRemap(CPed * ped, int index)
{
	PedExtended &info = extData.Get(ped);
	fs << "Remaps: Get remap num: " << info.curRemapNum[index] << " index " << index << " for ped " << (int)ped << "\n";
	return info.curRemapNum[index];
}

extern "C" void __declspec(dllexport) Ext_SetPedRemap(CPed * ped, int index, int num)
{
	PedExtended &info = extData.Get(ped);
	info.curRemapNum[index] = num;
	fs << "Remaps: New remap num: " << info.curRemapNum[index] << " index " << index << " for ped " << (int)ped << "\n";
}

extern "C" void __declspec(dllexport) ignore2() { return; };
