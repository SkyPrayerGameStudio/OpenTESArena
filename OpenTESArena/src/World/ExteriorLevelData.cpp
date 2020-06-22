#include <algorithm>
#include <iomanip>

#include "ExteriorLevelData.h"
#include "WorldType.h"
#include "../Assets/COLFile.h"
#include "../Assets/MIFUtils.h"
#include "../Assets/RMDFile.h"
#include "../Math/Random.h"
#include "../Media/PaletteFile.h"
#include "../Media/PaletteName.h"
#include "../Rendering/Renderer.h"
#include "../World/LocationType.h"
#include "../World/LocationUtils.h"
#include "../World/VoxelDataType.h"

#include "components/debug/Debug.h"
#include "components/utilities/Bytes.h"
#include "components/utilities/String.h"

ExteriorLevelData::ExteriorLevelData(int gridWidth, int gridHeight, int gridDepth,
	const std::string &infName, const std::string &name)
	: LevelData(gridWidth, gridHeight, gridDepth, infName, name) { }

ExteriorLevelData::~ExteriorLevelData()
{

}

void ExteriorLevelData::generateCity(uint32_t citySeed, int cityDim, WEInt gridDepth,
	const std::vector<uint8_t> &reservedBlocks, const Int2 &startPosition, ArenaRandom &random,
	const MiscAssets &miscAssets, std::vector<uint16_t> &dstFlor,
	std::vector<uint16_t> &dstMap1, std::vector<uint16_t> &dstMap2)
{
	// Decide which city blocks to load.
	enum class BlockType
	{
		Empty, Reserved, Equipment, MagesGuild,
		NobleHouse, Temple, Tavern, Spacer, Houses
	};

	// Get the city's local X and Y, to be used later for building name generation.
	const Int2 localCityPoint = LocationUtils::getLocalCityPoint(citySeed);

	const int citySize = cityDim * cityDim;
	std::vector<BlockType> plan(citySize, BlockType::Empty);

	auto placeBlock = [citySize, &plan, &random](BlockType blockType)
	{
		int planIndex;

		do
		{
			planIndex = random.next() % citySize;
		} while (plan.at(planIndex) != BlockType::Empty);

		plan.at(planIndex) = blockType;
	};

	// Set reserved blocks.
	for (const uint8_t block : reservedBlocks)
	{
		// The original engine uses a fixed array so all block indices always fall within the
		// plan, but since a dynamic array is used here, it has to ignore out-of-bounds blocks
		// explicitly.
		if (block < plan.size())
		{
			plan.at(block) = BlockType::Reserved;
		}
	}

	// Initial block placement.
	placeBlock(BlockType::Equipment);
	placeBlock(BlockType::MagesGuild);
	placeBlock(BlockType::NobleHouse);
	placeBlock(BlockType::Temple);
	placeBlock(BlockType::Tavern);
	placeBlock(BlockType::Spacer);

	// Create city plan according to RNG.
	const int emptyBlocksInPlan = static_cast<int>(
		std::count(plan.begin(), plan.end(), BlockType::Empty));
	for (int remainingBlocks = emptyBlocksInPlan; remainingBlocks > 0; remainingBlocks--)
	{
		const uint32_t randVal = random.next();
		const BlockType blockType = [randVal]()
		{
			if (randVal <= 0x7333)
			{
				return BlockType::Houses;
			}
			else if (randVal <= 0xA666)
			{
				return BlockType::Tavern;
			}
			else if (randVal <= 0xCCCC)
			{
				return BlockType::Equipment;
			}
			else if (randVal <= 0xE666)
			{
				return BlockType::Temple;
			}
			else
			{
				return BlockType::NobleHouse;
			}
		}();

		placeBlock(blockType);
	}

	// Build the city, loading data for each block. Load blocks right to left, top to bottom.
	int xDim = 0;
	int yDim = 0;

	for (const BlockType block : plan)
	{
		if (block != BlockType::Reserved)
		{
			const int blockIndex = static_cast<int>(block) - 2;
			const std::string &blockCode = MIFUtils::getCityBlockCode(blockIndex);
			const std::string &rotation = MIFUtils::getCityBlockRotation(
				random.next() % MIFUtils::getCityBlockRotationCount());
			const int variationCount = MIFUtils::getCityBlockVariations(blockIndex);
			const int variation = std::max(random.next() % variationCount, 1);
			const std::string blockMifName = MIFUtils::makeCityBlockMifName(
				blockCode.c_str(), variation, rotation.c_str());

			// Load the block's .MIF data into the level.
			const auto &cityBlockMifs = miscAssets.getCityBlockMifs();
			const auto iter = cityBlockMifs.find(blockMifName);
			if (iter == cityBlockMifs.end())
			{
				DebugCrash("Could not find .MIF file \"" + blockMifName + "\".");
			}

			const MIFFile &blockMif = iter->second;
			const auto &blockLevel = blockMif.getLevels().front();

			// Offset of the block in the voxel grid.
			const int xOffset = startPosition.x + (xDim * 20);
			const int zOffset = startPosition.y + (yDim * 20);

			// Copy block data to temp buffers.
			for (int z = 0; z < blockMif.getDepth(); z++)
			{
				const int srcIndex = z * blockMif.getWidth();
				const int dstIndex = xOffset + ((z + zOffset) * gridDepth);

				auto writeRow = [&blockMif, srcIndex, dstIndex](
					const std::vector<uint16_t> &src, std::vector<uint16_t> &dst)
				{
					const auto srcBegin = src.begin() + srcIndex;
					const auto srcEnd = srcBegin + blockMif.getWidth();
					const auto dstBegin = dst.begin() + dstIndex;
					std::copy(srcBegin, srcEnd, dstBegin);
				};

				writeRow(blockLevel.flor, dstFlor);
				writeRow(blockLevel.map1, dstMap1);
				writeRow(blockLevel.map2, dstMap2);
			}
		}

		xDim++;

		// Move to the next row if done with the current one.
		if (xDim == cityDim)
		{
			xDim = 0;
			yDim++;
		}
	}
}

void ExteriorLevelData::generateBuildingNames(const LocationDefinition &locationDef,
	const ProvinceDefinition &provinceDef, ArenaRandom &random, bool isCity,
	SNInt gridWidth, WEInt gridDepth, const MiscAssets &miscAssets)
{
	const auto &exeData = miscAssets.getExeData();
	const LocationDefinition::CityDefinition &cityDef = locationDef.getCityDefinition();
	
	uint32_t citySeed = cityDef.citySeed;
	const Int2 localCityPoint = LocationUtils::getLocalCityPoint(citySeed);

	// Lambda for looping through main-floor voxels and generating names for *MENU blocks that
	// match the given menu type.
	auto generateNames = [this, &provinceDef, &citySeed, &random, isCity,
		gridWidth, gridDepth, &miscAssets, &exeData, &cityDef, &localCityPoint](
			VoxelDefinition::WallData::MenuType menuType)
	{
		if ((menuType == VoxelDefinition::WallData::MenuType::Equipment) ||
			(menuType == VoxelDefinition::WallData::MenuType::Temple))
		{
			citySeed = (localCityPoint.x << 16) + localCityPoint.y;
			random.srand(citySeed);
		}

		std::vector<int> seen;
		auto hashInSeen = [&seen](int hash)
		{
			return std::find(seen.begin(), seen.end(), hash) != seen.end();
		};

		// Lambdas for creating tavern, equipment store, and temple building names.
		auto createTavernName = [&exeData, &cityDef](int m, int n)
		{
			const auto &tavernPrefixes = exeData.cityGen.tavernPrefixes;
			const auto &tavernSuffixes = cityDef.coastal ?
				exeData.cityGen.tavernMarineSuffixes : exeData.cityGen.tavernSuffixes;
			return tavernPrefixes.at(m) + ' ' + tavernSuffixes.at(n);
		};

		auto createEquipmentName = [&provinceDef, &random, gridWidth, gridDepth, &miscAssets,
			&exeData, &cityDef](int m, int n, int x, int z)
		{
			const auto &equipmentPrefixes = exeData.cityGen.equipmentPrefixes;
			const auto &equipmentSuffixes = exeData.cityGen.equipmentSuffixes;

			// Equipment store names can have variables in them.
			std::string str = equipmentPrefixes.at(m) + ' ' + equipmentSuffixes.at(n);

			// Replace %ct with city type name.
			size_t index = str.find("%ct");
			if (index != std::string::npos)
			{
				const std::string_view cityTypeName = cityDef.typeDisplayName;
				str.replace(index, 3, cityTypeName);
			}

			// Replace %ef with generated male first name from (y<<16)+x seed. Use a local RNG for
			// modifications to building names. Swap and reverse the XZ dimensions so they fit the
			// original XY values in Arena.
			index = str.find("%ef");
			if (index != std::string::npos)
			{
				ArenaRandom nameRandom((x << 16) + z);
				const bool isMale = true;
				const std::string maleFirstName = [&provinceDef, &miscAssets, isMale, &nameRandom]()
				{
					const std::string name = miscAssets.generateNpcName(
						provinceDef.getRaceID(), isMale, nameRandom);
					const std::string firstName = String::split(name).front();
					return firstName;
				}();

				str.replace(index, 3, maleFirstName);
			}

			// Replace %n with generated male name from (x<<16)+y seed.
			index = str.find("%n");
			if (index != std::string::npos)
			{
				ArenaRandom nameRandom((z << 16) + x);
				const bool isMale = true;
				const std::string maleName = miscAssets.generateNpcName(
					provinceDef.getRaceID(), isMale, nameRandom);
				str.replace(index, 2, maleName);
			}

			return str;
		};

		auto createTempleName = [&exeData](int model, int n)
		{
			const auto &templePrefixes = exeData.cityGen.templePrefixes;
			const auto &temple1Suffixes = exeData.cityGen.temple1Suffixes;
			const auto &temple2Suffixes = exeData.cityGen.temple2Suffixes;
			const auto &temple3Suffixes = exeData.cityGen.temple3Suffixes;

			const std::string &templeSuffix = [&temple1Suffixes, &temple2Suffixes,
				&temple3Suffixes, model, n]() -> const std::string&
			{
				if (model == 0)
				{
					return temple1Suffixes.at(n);
				}
				else if (model == 1)
				{
					return temple2Suffixes.at(n);
				}
				else
				{
					return temple3Suffixes.at(n);
				}
			}();

			// No extra whitespace needed, I think?
			return templePrefixes.at(model) + templeSuffix;
		};

		// The lambda called for each main-floor voxel in the area.
		auto tryGenerateBlockName = [this, isCity, menuType, &random, &seen, &hashInSeen,
			&createTavernName, &createEquipmentName, &createTempleName](int x, int z)
		{
			// See if the current voxel is a *MENU block and matches the target menu type.
			const bool matchesTargetType = [this, isCity, x, z, menuType]()
			{
				const auto &voxelGrid = this->getVoxelGrid();
				const uint16_t voxelID = voxelGrid.getVoxel(x, 1, z);
				const VoxelDefinition &voxelDef = voxelGrid.getVoxelDef(voxelID);
				return (voxelDef.dataType == VoxelDataType::Wall) && voxelDef.wall.isMenu() &&
					(VoxelDefinition::WallData::getMenuType(voxelDef.wall.menuID, isCity) == menuType);
			}();

			if (matchesTargetType)
			{
				// Get the *MENU block's display name.
				int hash;
				std::string name;

				if (menuType == VoxelDefinition::WallData::MenuType::Tavern)
				{
					// Tavern.
					int m, n;
					do
					{
						m = random.next() % 23;
						n = random.next() % 23;
						hash = (m << 8) + n;
					} while (hashInSeen(hash));

					name = createTavernName(m, n);
				}
				else if (menuType == VoxelDefinition::WallData::MenuType::Equipment)
				{
					// Equipment store.
					int m, n;
					do
					{
						m = random.next() % 20;
						n = random.next() % 10;
						hash = (m << 8) + n;
					} while (hashInSeen(hash));

					name = createEquipmentName(m, n, x, z);
				}
				else
				{
					// Temple.
					int model, n;
					do
					{
						model = random.next() % 3;
						const std::array<int, 3> ModelVars = { 5, 9, 10 };
						const int vars = ModelVars.at(model);
						n = random.next() % vars;
						hash = (model << 8) + n;
					} while (hashInSeen(hash));

					name = createTempleName(model, n);
				}

				this->menuNames.push_back(std::make_pair(Int2(x, z), std::move(name)));
				seen.push_back(hash);
			}
		};

		// Start at the top-right corner of the map, running right to left and top to bottom.
		for (int x = gridWidth - 1; x >= 0; x--)
		{
			for (int z = gridDepth - 1; z >= 0; z--)
			{
				tryGenerateBlockName(x, z);
			}
		}

		// Fix some edge cases used with the main quest.
		if ((menuType == VoxelDefinition::WallData::MenuType::Temple) &&
			cityDef.hasMainQuestTempleOverride)
		{
			const auto &mainQuestTempleOverride = cityDef.mainQuestTempleOverride;
			const int modelIndex = mainQuestTempleOverride.modelIndex;
			const int suffixIndex = mainQuestTempleOverride.suffixIndex;

			// Added an index variable since the original game seems to store its menu names in a
			// way other than with a vector like this solution is using.
			const int menuNamesIndex = mainQuestTempleOverride.menuNamesIndex;

			DebugAssertIndex(this->menuNames, menuNamesIndex);
			this->menuNames[menuNamesIndex].second = createTempleName(modelIndex, suffixIndex);
		}
	};

	generateNames(VoxelDefinition::WallData::MenuType::Tavern);
	generateNames(VoxelDefinition::WallData::MenuType::Equipment);
	generateNames(VoxelDefinition::WallData::MenuType::Temple);
}

void ExteriorLevelData::generateWildChunkBuildingNames(const ExeData &exeData)
{
	// Lambda for looping through main-floor voxels and generating names for *MENU blocks that
	// match the given menu type.
	auto generateNames = [this, &exeData](int wildX, int wildY,
		VoxelDefinition::WallData::MenuType menuType)
	{
		const uint32_t wildChunkSeed = (wildY << 16) + wildX;

		// Don't need hashInSeen() for the wilderness.

		// Lambdas for creating tavern and temple building names.
		auto createTavernName = [&exeData](int m, int n)
		{
			const auto &tavernPrefixes = exeData.cityGen.tavernPrefixes;
			const auto &tavernSuffixes = exeData.cityGen.tavernSuffixes;
			return tavernPrefixes.at(m) + ' ' + tavernSuffixes.at(n);
		};

		auto createTempleName = [&exeData](int model, int n)
		{
			const auto &templePrefixes = exeData.cityGen.templePrefixes;
			const auto &temple1Suffixes = exeData.cityGen.temple1Suffixes;
			const auto &temple2Suffixes = exeData.cityGen.temple2Suffixes;
			const auto &temple3Suffixes = exeData.cityGen.temple3Suffixes;

			const std::string &templeSuffix = [&temple1Suffixes, &temple2Suffixes,
				&temple3Suffixes, model, n]() -> const std::string&
			{
				if (model == 0)
				{
					return temple1Suffixes.at(n);
				}
				else if (model == 1)
				{
					return temple2Suffixes.at(n);
				}
				else
				{
					return temple3Suffixes.at(n);
				}
			}();

			// No extra whitespace needed, I think?
			return templePrefixes.at(model) + templeSuffix;
		};

		// The lambda called for each main-floor voxel in the area.
		auto tryGenerateBlockName = [this, wildX, wildY, wildChunkSeed, menuType, &createTavernName,
			&createTempleName](int x, int z)
		{
			ArenaRandom random(wildChunkSeed);

			// Make sure the coordinate math is done in the new coordinate system.
			const Int2 relativeOrigin(
				((RMDFile::DEPTH - 1) - wildX) * RMDFile::DEPTH,
				((RMDFile::WIDTH - 1) - wildY) * RMDFile::WIDTH);
			const Int2 dstPoint(
				relativeOrigin.y + (RMDFile::WIDTH - 1 - x),
				relativeOrigin.x + (RMDFile::DEPTH - 1 - z));

			// See if the current voxel is a *MENU block and matches the target menu type.
			const bool matchesTargetType = [this, &dstPoint, menuType]()
			{
				const auto &voxelGrid = this->getVoxelGrid();
				const bool isCity = false; // Wilderness only.
				const uint16_t voxelID = voxelGrid.getVoxel(dstPoint.x, 1, dstPoint.y);
				const VoxelDefinition &voxelDef = voxelGrid.getVoxelDef(voxelID);
				return (voxelDef.dataType == VoxelDataType::Wall) && voxelDef.wall.isMenu() &&
					(VoxelDefinition::WallData::getMenuType(voxelDef.wall.menuID, isCity) == menuType);
			}();

			if (matchesTargetType)
			{
				// Get the *MENU block's display name.
				const std::string name = [menuType, &random, &createTavernName, &createTempleName]()
				{
					if (menuType == VoxelDefinition::WallData::MenuType::Tavern)
					{
						// Tavern.
						const int m = random.next() % 23;
						const int n = random.next() % 23;
						return createTavernName(m, n);
					}
					else
					{
						// Temple.
						const int model = random.next() % 3;
						constexpr std::array<int, 3> ModelVars = { 5, 9, 10 };
						const int vars = ModelVars.at(model);
						const int n = random.next() % vars;
						return createTempleName(model, n);
					}
				}();

				this->menuNames.push_back(std::make_pair(dstPoint, std::move(name)));
			}
		};

		// Iterate blocks in the chunk in any order. They are order-independent in the wild.
		for (int x = 0; x < RMDFile::DEPTH; x++)
		{
			for (int z = 0; z < RMDFile::WIDTH; z++)
			{
				tryGenerateBlockName(x, z);
			}
		}
	};

	// Iterate over each wild chunk.
	const int wildChunksPerSide = 64;
	for (int y = 0; y < wildChunksPerSide; y++)
	{
		for (int x = 0; x < wildChunksPerSide; x++)
		{
			generateNames(x, y, VoxelDefinition::WallData::MenuType::Tavern);
			generateNames(x, y, VoxelDefinition::WallData::MenuType::Temple);
		}
	}
}

void ExteriorLevelData::revisePalaceGraphics(std::vector<uint16_t> &map1, SNInt gridWidth, WEInt gridDepth)
{
	// Lambda for obtaining a two-byte MAP1 voxel.
	auto getMap1Voxel = [&map1, gridWidth, gridDepth](SNInt x, WEInt z)
	{
		const int index = (z * 2) + ((x * 2) * gridDepth);
		const uint16_t voxel = Bytes::getLE16(reinterpret_cast<const uint8_t*>(map1.data()) + index);
		return voxel;
	};

	auto setMap1Voxel = [&map1, gridWidth, gridDepth](SNInt x, WEInt z, uint16_t voxel)
	{
		const int index = z + (x * gridDepth);
		DebugAssertIndex(map1, index);
		map1[index] = voxel;
	};

	struct SearchResult
	{
		enum class Side { None, North, South, East, West };

		Side side;

		// Distance from the associated origin dimension, where (0, 0) is at the top right.
		int offset;

		SearchResult(Side side, int offset)
		{
			this->side = side;
			this->offset = offset;
		}
	};

	// Find one of the palace graphic blocks, then extrapolate the positions of
	// the other palace graphic and the gates.
	const SearchResult result = [gridWidth, gridDepth, &getMap1Voxel]()
	{
		auto isPalaceBlock = [&getMap1Voxel](SNInt x, WEInt z)
		{
			const uint16_t voxel = getMap1Voxel(x, z);
			const uint8_t mostSigNibble = (voxel & 0xF000) >> 12;
			return mostSigNibble == 0x9;
		};

		// North (top edge) and south (bottom edge), search right to left.
		for (WEInt z = 1; z < (gridDepth - 1); z++)
		{
			const SNInt northX = 0;
			const SNInt southX = gridWidth - 1;
			if (isPalaceBlock(northX, z))
			{
				return SearchResult(SearchResult::Side::North, z);
			}
			else if (isPalaceBlock(southX, z))
			{
				return SearchResult(SearchResult::Side::South, z);
			}
		}

		// East (right edge) and west (left edge), search top to bottom.
		for (SNInt x = 1; x < (gridWidth - 1); x++)
		{
			const WEInt eastZ = 0;
			const WEInt westZ = gridDepth - 1;
			if (isPalaceBlock(x, eastZ))
			{
				return SearchResult(SearchResult::Side::East, x);
			}
			else if (isPalaceBlock(x, westZ))
			{
				return SearchResult(SearchResult::Side::West, x);
			}
		}

		// No palace gate found. This should never happen because every city/town/village
		// in the original game has a palace gate somewhere.
		return SearchResult(SearchResult::Side::None, 0);
	}();

	// Decide how to extrapolate the search results.
	if (result.side != SearchResult::Side::None)
	{
		// The direction to step from a palace voxel to the other palace voxel.
		const NewInt2 northSouthPalaceStep(0, -1);
		const NewInt2 eastWestPalaceStep(-1, 0);

		// Gets the distance in voxels from a palace voxel to its gate, or -1 if no gate exists.
		const int NO_GATE = -1;
		auto getGateDistance = [&getMap1Voxel, NO_GATE](const NewInt2 &palaceVoxel, const NewInt2 &dir)
		{
			auto isGateBlock = [&getMap1Voxel](SNInt x, WEInt z)
			{
				const uint16_t voxel = getMap1Voxel(x, z);
				const uint8_t mostSigNibble = (voxel & 0xF000) >> 12;
				return mostSigNibble == 0xA;
			};

			// Gates should usually be within a couple blocks of their castle graphic. If not,
			// then no gate exists.
			const int MAX_GATE_DIST = 8;

			int i = 0;
			NewInt2 position = palaceVoxel;
			while ((i < MAX_GATE_DIST) && !isGateBlock(position.x, position.y))
			{
				position = position + dir;
				i++;
			}

			return (i < MAX_GATE_DIST) ? i : NO_GATE;
		};

		// Set the positions of the two palace voxels and the two gate voxels.
		NewInt2 firstPalaceVoxel, secondPalaceVoxel, firstGateVoxel, secondGateVoxel;
		uint16_t firstPalaceVoxelID, secondPalaceVoxelID, gateVoxelID;
		int gateDist;
		if (result.side == SearchResult::Side::North)
		{
			firstPalaceVoxel = NewInt2(gridWidth - 1, result.offset);
			secondPalaceVoxel = firstPalaceVoxel + northSouthPalaceStep;
			const NewInt2 gateDir(-1, 0);
			gateDist = getGateDistance(firstPalaceVoxel, gateDir);
			firstGateVoxel = firstPalaceVoxel + (gateDir * gateDist);
			secondGateVoxel = firstGateVoxel + northSouthPalaceStep;
			firstPalaceVoxelID = 0xA5B5;
			secondPalaceVoxelID = 0xA5B4;
			gateVoxelID = 0xA1B3;
		}
		else if (result.side == SearchResult::Side::South)
		{
			firstPalaceVoxel = NewInt2(0, result.offset);
			secondPalaceVoxel = firstPalaceVoxel + northSouthPalaceStep;
			const NewInt2 gateDir(1, 0);
			gateDist = getGateDistance(firstPalaceVoxel, gateDir);
			firstGateVoxel = firstPalaceVoxel + (gateDir * gateDist);
			secondGateVoxel = firstGateVoxel + northSouthPalaceStep;
			firstPalaceVoxelID = 0xA534;
			secondPalaceVoxelID = 0xA535;
			gateVoxelID = 0xA133;
		}
		else if (result.side == SearchResult::Side::East)
		{
			firstPalaceVoxel = NewInt2(result.offset, gridDepth - 1);
			secondPalaceVoxel = firstPalaceVoxel + eastWestPalaceStep;
			const NewInt2 gateDir(0, -1);
			gateDist = getGateDistance(firstPalaceVoxel, gateDir);
			firstGateVoxel = firstPalaceVoxel + (gateDir * gateDist);
			secondGateVoxel = firstGateVoxel + eastWestPalaceStep;
			firstPalaceVoxelID = 0xA574;
			secondPalaceVoxelID = 0xA575;
			gateVoxelID = 0xA173;
		}
		else if (result.side == SearchResult::Side::West)
		{
			firstPalaceVoxel = NewInt2(result.offset, 0);
			secondPalaceVoxel = firstPalaceVoxel + eastWestPalaceStep;
			const NewInt2 gateDir(0, 1);
			gateDist = getGateDistance(firstPalaceVoxel, gateDir);
			firstGateVoxel = firstPalaceVoxel + (gateDir * gateDist);
			secondGateVoxel = firstGateVoxel + eastWestPalaceStep;
			firstPalaceVoxelID = 0xA5F5;
			secondPalaceVoxelID = 0xA5F4;
			gateVoxelID = 0xA1F3;
		}

		// Set the voxel IDs to their new values.
		setMap1Voxel(firstPalaceVoxel.x, firstPalaceVoxel.y, firstPalaceVoxelID);
		setMap1Voxel(secondPalaceVoxel.x, secondPalaceVoxel.y, secondPalaceVoxelID);

		if (gateDist != NO_GATE)
		{
			setMap1Voxel(firstGateVoxel.x, firstGateVoxel.y, gateVoxelID);
			setMap1Voxel(secondGateVoxel.x, secondGateVoxel.y, gateVoxelID);
		}
	}
	else
	{
		// The search did not find any palace graphics block.
		DebugLogWarning("No palace graphics found to revise.");
	}
}

Buffer2D<uint8_t> ExteriorLevelData::generateWildernessIndices(uint32_t wildSeed,
	const ExeData::Wilderness &wildData)
{
	const int wildWidth = 64;
	const int wildHeight = 64;
	Buffer2D<uint8_t> indices(wildWidth, wildHeight);
	ArenaRandom random(wildSeed);

	// Generate a random wilderness .MIF index for each wilderness chunk.
	std::generate(indices.get(), indices.get() + (indices.getWidth() * indices.getHeight()),
		[&wildData, &random]()
	{
		// Determine the wilderness block list to draw from.
		const auto &blockList = [&wildData, &random]() -> const std::vector<uint8_t>&
		{
			const uint16_t normalVal = 0x6666;
			const uint16_t villageVal = 0x4000;
			const uint16_t dungeonVal = 0x2666;
			const uint16_t tavernVal = 0x1999;
			int randVal = random.next();

			if (randVal < normalVal)
			{
				return wildData.normalBlocks;
			}
			else
			{
				randVal -= normalVal;
				if (randVal < villageVal)
				{
					return wildData.villageBlocks;
				}
				else
				{
					randVal -= villageVal;
					if (randVal < dungeonVal)
					{
						return wildData.dungeonBlocks;
					}
					else
					{
						randVal -= dungeonVal;
						if (randVal < tavernVal)
						{
							return wildData.tavernBlocks;
						}
						else
						{
							return wildData.templeBlocks;
						}
					}
				}
			}
		}();

		const int blockListIndex = (random.next() & 0xFF) % blockList.size();
		return blockList[blockListIndex];
	});

	// City indices in the center of the wilderness (WILD001.MIF, etc.).
	DebugAssertMsg(wildWidth >= 2, "Wild width \"" + std::to_string(wildWidth) + "\" too small.");
	DebugAssertMsg(wildHeight >= 2, "Wild height \"" + std::to_string(wildHeight) + "\" too small.");
	const int cityX = (wildWidth / 2) - 1;
	const int cityY = (wildHeight / 2) - 1;
	indices.set(cityX, cityY, 1);
	indices.set(cityX + 1, cityY, 2);
	indices.set(cityX, cityY + 1, 3);
	indices.set(cityX + 1, cityY + 1, 4);

	return indices;
}

void ExteriorLevelData::reviseWildernessCity(const LocationDefinition &locationDef,
	Buffer2D<uint16_t> &flor, Buffer2D<uint16_t> &map1, Buffer2D<uint16_t> &map2,
	const MiscAssets &miscAssets)
{
	// For now, assume the given buffers are for the entire 4096x4096 wilderness.
	// @todo: change to only care about 128x128 layers.
	DebugAssert(flor.getWidth() == (64 * RMDFile::WIDTH));
	DebugAssert(flor.getWidth() == flor.getHeight());
	DebugAssert(flor.getWidth() == map1.getWidth());
	DebugAssert(flor.getWidth() == map2.getWidth());

	// Clear all placeholder city blocks.
	const int placeholderWidth = RMDFile::WIDTH * 2;
	const int placeholderDepth = RMDFile::DEPTH * 2;

	// @todo: change to only care about 128x128 floors -- these should both be removed.
	const int xOffset = RMDFile::WIDTH * 31;
	const int zOffset = RMDFile::DEPTH * 31;

	for (int x = 0; x < placeholderWidth; x++)
	{
		const int startIndex = zOffset + ((x + xOffset) * flor.getWidth());

		auto clearRow = [placeholderDepth, startIndex](Buffer2D<uint16_t> &dst)
		{
			const auto dstBegin = dst.get() + startIndex;
			const auto dstEnd = dstBegin + placeholderDepth;
			DebugAssert(dstEnd <= dst.end());
			std::fill(dstBegin, dstEnd, 0);
		};

		clearRow(flor);
		clearRow(map1);
		clearRow(map2);
	}

	// Get city generation info -- the .MIF filename to load for the city skeleton.
	const LocationDefinition::CityDefinition &cityDef = locationDef.getCityDefinition();
	const std::string mifName = cityDef.mapFilename;
	MIFFile mif;
	if (!mif.init(mifName.c_str()))
	{
		DebugLogError("Couldn't init .MIF file \"" + mifName + "\".");
		return;
	}

	const MIFFile::Level &level = mif.getLevels().front();

	// Buffers for the city data. Copy the .MIF data into them.
	std::vector<uint16_t> cityFlor(level.flor.begin(), level.flor.end());
	std::vector<uint16_t> cityMap1(level.map1.begin(), level.map1.end());
	std::vector<uint16_t> cityMap2(level.map2.begin(), level.map2.end());

	// Run city generation if it's not a premade city. The center province's city does not have
	// any special generation -- the .MIF buffers are simply used as-is (with some simple palace
	// gate revisions done afterwards).
	const bool isPremadeCity = cityDef.premade;
	if (!isPremadeCity)
	{
		const int cityBlocksPerSide = cityDef.cityBlocksPerSide;
		const std::vector<uint8_t> &reservedBlocks = *cityDef.reservedBlocks;
		const OriginalInt2 blockStartPosition(cityDef.blockStartPosX, cityDef.blockStartPosY);
		const uint32_t citySeed = cityDef.citySeed;
		ArenaRandom random(citySeed);

		// Write generated city data into the temp city buffers.
		ExteriorLevelData::generateCity(citySeed, cityBlocksPerSide, mif.getWidth(),
			reservedBlocks, blockStartPosition, random, miscAssets, cityFlor, cityMap1, cityMap2);
	}

	// Transform city voxels based on the wilderness rules.
	for (WEInt x = 0; x < mif.getWidth(); x++)
	{
		for (SNInt z = 0; z < mif.getDepth(); z++)
		{
			const int index = DebugMakeIndex(cityFlor, z + (x * mif.getDepth()));
			uint16_t &map1Voxel = cityMap1[index];
			uint16_t &map2Voxel = cityMap2[index];

			if ((map1Voxel & 0x8000) != 0)
			{
				map1Voxel = 0;
				map2Voxel = 0;
			}
			else
			{
				const bool isWall = (map1Voxel == 0x2F2F) || (map1Voxel == 0x2D2D) || (map1Voxel == 0x2E2E);
				if (!isWall)
				{
					map1Voxel = 0;
					map2Voxel = 0;
				}
				else
				{
					// Replace solid walls.
					if (map1Voxel == 0x2F2F)
					{
						map1Voxel = 0x3030;
						map2Voxel = 0x3030 | (map2Voxel & 0x8080);
					}
					else if (map1Voxel == 0x2D2D)
					{
						map1Voxel = 0x2F2F;
						map2Voxel = 0x3030 | (map2Voxel & 0x8080);
					}
					else if (map1Voxel == 0x2E2E)
					{
						map2Voxel = 0x3030 | (map2Voxel & 0x8080);
					}
				}
			}
		}
	}

	// Write city buffers into the wilderness.
	for (SNInt z = 0; z < mif.getDepth(); z++)
	{
		const int srcIndex = DebugMakeIndex(cityFlor, z * mif.getWidth());
		const int dstIndex = xOffset + ((z + zOffset) * flor.getWidth());

		auto writeRow = [&mif, srcIndex, dstIndex](
			const std::vector<uint16_t> &src, Buffer2D<uint16_t> &dst)
		{
			const auto srcBegin = src.begin() + srcIndex;
			const auto srcEnd = srcBegin + mif.getWidth();
			const auto dstBegin = dst.get() + dstIndex;
			DebugAssert((dstBegin + std::distance(srcBegin, srcEnd)) <= dst.end());
			std::copy(srcBegin, srcEnd, dstBegin);
		};

		writeRow(cityFlor, flor);
		writeRow(cityMap1, map1);
		writeRow(cityMap2, map2);
	}
}

OriginalInt2 ExteriorLevelData::getRelativeWildOrigin(const Int2 &voxel)
{
	return OriginalInt2(
		voxel.x - (voxel.x % (RMDFile::WIDTH * 2)),
		voxel.y - (voxel.y % (RMDFile::DEPTH * 2)));
}

NewInt2 ExteriorLevelData::getCenteredWildOrigin(const NewInt2 &voxel)
{
	return NewInt2(
		(std::max(voxel.x - 32, 0) / RMDFile::WIDTH) * RMDFile::WIDTH,
		(std::max(voxel.y - 32, 0) / RMDFile::DEPTH) * RMDFile::DEPTH);
}

ExteriorLevelData ExteriorLevelData::loadCity(const LocationDefinition &locationDef,
	const ProvinceDefinition &provinceDef, const MIFFile::Level &level, WeatherType weatherType,
	int currentDay, int starCount, const std::string &infName, int gridWidth, int gridDepth,
	const MiscAssets &miscAssets, TextureManager &textureManager)
{
	// Create temp voxel data buffers and write the city skeleton data to them. Each city
	// block will be written to them as well.
	std::vector<uint16_t> tempFlor(level.flor.begin(), level.flor.end());
	std::vector<uint16_t> tempMap1(level.map1.begin(), level.map1.end());
	std::vector<uint16_t> tempMap2(level.map2.begin(), level.map2.end());

	// Get the city's seed for random chunk generation. It is modified later during
	// building name generation.
	const LocationDefinition::CityDefinition &cityDef = locationDef.getCityDefinition();
	const uint32_t citySeed = cityDef.citySeed;
	ArenaRandom random(citySeed);

	if (!cityDef.premade)
	{
		// Generate procedural city data and write it into the temp buffers.
		const std::vector<uint8_t> &reservedBlocks = *cityDef.reservedBlocks;
		const OriginalInt2 blockStartPosition(cityDef.blockStartPosX, cityDef.blockStartPosY);
		ExteriorLevelData::generateCity(citySeed, cityDef.cityBlocksPerSide, gridDepth,
			reservedBlocks, blockStartPosition, random, miscAssets, tempFlor, tempMap1, tempMap2);
	}

	// Run the palace gate graphic algorithm over the perimeter of the MAP1 data.
	ExteriorLevelData::revisePalaceGraphics(tempMap1, gridWidth, gridDepth);

	// Create the level for the voxel data to be written into.
	ExteriorLevelData levelData(gridWidth, level.getHeight(), gridDepth, infName, level.name);

	// Load FLOR, MAP1, and MAP2 voxels into the voxel grid.
	const auto &exeData = miscAssets.getExeData();
	const INFFile &inf = levelData.getInfFile();
	levelData.readFLOR(tempFlor.data(), inf, gridWidth, gridDepth);
	levelData.readMAP1(tempMap1.data(), inf, WorldType::City, gridWidth, gridDepth, exeData);
	levelData.readMAP2(tempMap2.data(), inf, gridWidth, gridDepth);

	// Generate building names.
	const bool isCity = true;
	levelData.generateBuildingNames(locationDef, provinceDef, random, isCity,
		gridWidth, gridDepth, miscAssets);

	// Generate distant sky.
	levelData.distantSky.init(locationDef, provinceDef, weatherType, currentDay,
		starCount, exeData, textureManager);

	return levelData;
}

ExteriorLevelData ExteriorLevelData::loadWilderness(const LocationDefinition &locationDef,
	const ProvinceDefinition &provinceDef, WeatherType weatherType, int currentDay,
	int starCount, const std::string &infName, const MiscAssets &miscAssets,
	TextureManager &textureManager)
{
	const LocationDefinition::CityDefinition &cityDef = locationDef.getCityDefinition();
	const auto &wildData = miscAssets.getExeData().wild;
	const Buffer2D<uint8_t> wildIndices =
		ExteriorLevelData::generateWildernessIndices(cityDef.wildSeed, wildData);

	// Temp buffers for voxel data.
	Buffer2D<uint16_t> tempFlor(RMDFile::DEPTH * wildIndices.getWidth(),
		RMDFile::WIDTH * wildIndices.getHeight());
	Buffer2D<uint16_t> tempMap1(tempFlor.getWidth(), tempFlor.getHeight());
	Buffer2D<uint16_t> tempMap2(tempFlor.getWidth(), tempFlor.getHeight());
	tempFlor.fill(0);
	tempMap1.fill(0);
	tempMap2.fill(0);

	auto writeRMD = [&miscAssets, &tempFlor, &tempMap1, &tempMap2](
		uint8_t rmdID, int xOffset, int zOffset)
	{
		const std::vector<RMDFile> &rmdFiles = miscAssets.getWildernessChunks();
		const int rmdIndex = DebugMakeIndex(rmdFiles, rmdID - 1);
		const RMDFile &rmd = rmdFiles[rmdIndex];

		// Copy .RMD voxel data to temp buffers.
		for (int z = 0; z < RMDFile::DEPTH; z++)
		{
			const int srcIndex = z * RMDFile::DEPTH;
			const int dstIndex = xOffset + ((z + zOffset) * tempFlor.getWidth());

			auto writeRow = [srcIndex, dstIndex](const std::vector<uint16_t> &src,
				Buffer2D<uint16_t> &dst)
			{
				const auto srcBegin = src.begin() + srcIndex;
				const auto srcEnd = srcBegin + RMDFile::DEPTH;
				const auto dstBegin = dst.get() + dstIndex;
				DebugAssert((dstBegin + std::distance(srcBegin, srcEnd)) <= dst.end());
				std::copy(srcBegin, srcEnd, dstBegin);
			};

			writeRow(rmd.getFLOR(), tempFlor);
			writeRow(rmd.getMAP1(), tempMap1);
			writeRow(rmd.getMAP2(), tempMap2);
		}
	};

	// Load .RMD files into the wilderness, each at some X and Z offset in the voxel grid.
	for (int y = 0; y < wildIndices.getHeight(); y++)
	{
		for (int x = 0; x < wildIndices.getWidth(); x++)
		{
			const uint8_t wildIndex = wildIndices.get(x, y);
			writeRMD(wildIndex, x * RMDFile::WIDTH, y * RMDFile::DEPTH);
		}
	}

	// Change the placeholder WILD00{1..4}.MIF blocks to the ones for the given city.
	ExteriorLevelData::reviseWildernessCity(locationDef, tempFlor, tempMap1, tempMap2, miscAssets);

	// Create the level for the voxel data to be written into.
	const int levelHeight = 6;
	const std::string levelName = "WILD"; // Arbitrary
	ExteriorLevelData levelData(tempFlor.getWidth(), levelHeight, tempFlor.getHeight(),
		infName, levelName);

	// Load FLOR, MAP1, and MAP2 voxels into the voxel grid.
	const auto &exeData = miscAssets.getExeData();
	const INFFile &inf = levelData.getInfFile();
	levelData.readFLOR(tempFlor.get(), inf, tempFlor.getWidth(), tempFlor.getHeight());
	levelData.readMAP1(tempMap1.get(), inf, WorldType::Wilderness, tempMap1.getWidth(),
		tempMap1.getHeight(), exeData);
	levelData.readMAP2(tempMap2.get(), inf, tempMap1.getWidth(), tempMap1.getHeight());

	// Generate wilderness building names.
	levelData.generateWildChunkBuildingNames(exeData);

	// Generate distant sky.
	levelData.distantSky.init(locationDef, provinceDef, weatherType, currentDay,
		starCount, exeData, textureManager);

	return levelData;
}

const std::vector<std::pair<Int2, std::string>> &ExteriorLevelData::getMenuNames() const
{
	return this->menuNames;
}

bool ExteriorLevelData::isOutdoorDungeon() const
{
	return false;
}

void ExteriorLevelData::setActive(bool nightLightsAreActive, const WorldData &worldData,
	const LocationDefinition &locationDef, const MiscAssets &miscAssets,
	TextureManager &textureManager, Renderer &renderer)
{
	LevelData::setActive(nightLightsAreActive, worldData, locationDef, miscAssets,
		textureManager, renderer);

	// @todo: fetch this palette from somewhere better.
	COLFile col;
	const std::string colName = PaletteFile::fromName(PaletteName::Default);
	if (!col.init(colName.c_str()))
	{
		DebugCrash("Couldn't init .COL file \"" + colName + "\".");
	}

	// Give distant sky data to the renderer.
	renderer.setDistantSky(this->distantSky, col.getPalette());
}

void ExteriorLevelData::tick(Game &game, double dt)
{
	LevelData::tick(game, dt);
	this->distantSky.tick(dt);
}
